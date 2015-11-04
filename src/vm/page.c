#include "vm/struct.h"

// Initialise everything
void VM_init(void)
{
	int i;
	for (i = 0; i < NO_OF_LOCKS; i++)
	{
		lock_init(&l[i]);
	}
	hash_init(&hash_frame, frame_hash, frame_less_helper, NULL);
	hash_init(&hash_mmap, mmap_hash, mmap_less_helper, NULL);
	list_init(&hash_frame_list);
	swap_block = block_get_role(BLOCK_SWAP);
	swap_size = block_size(swap_block);
	swap_bitmap = bitmap_create(swap_size);
}

struct page_struct *VM_new_page(int type, void *virt_address, bool writable,
		struct file *file, off_t ofs, size_t read_bytes, size_t zero_bytes,
		off_t block_id)
{
	struct page_struct *p = (struct page_struct*) malloc(
			sizeof(struct page_struct));
	if (p != NULL)
	{
		//common data for both the types
		p->virtual_address = virt_address;
		p->physical_address = NULL;
		p->writable = writable;
		p->loaded = false;
		p->index = 0;
		p->pagedir = thread_current()->pagedir;
	}
	if (type == TYPE_ZERO)
	{

		if (p != NULL)
		{
			p->type = TYPE_ZERO;

			pagedir_op_page(p->pagedir, p->virtual_address, (void *) p);

			return p;
		}
	}
	else if (type == TYPE_FILE)
	{
		if (p != NULL)
		{
			p->type = TYPE_FILE;
			p->file = file;
			p->offset = ofs;
			p->read_bytes = read_bytes;
			p->zero_bytes = zero_bytes;
			p->bid = block_id;

			pagedir_op_page(p->pagedir, p->virtual_address, (void *) p);

			return p;
		}
	}
	return NULL;
}

//setting the operation to true pins the page
//returns true if the operation is successful
//directFrameAccess allows you to pin the frame
bool VM_pin(bool operation, void *pagetemp, bool directFrameAccess)
{
	struct page_struct *page = (struct page_struct *) pagetemp;
	void * address = page->physical_address;
	if (directFrameAccess)
		address = pagetemp;
	if (page->physical_address == NULL || directFrameAccess)
	{
		struct frame_struct *f = address_to_frame(address);
		if (f != NULL)
		{
			switch (operation)
			{
			case true:
				f->persistent = true;
				return true;
				break;
			case false:
				f->persistent = false;
				return true;
				break;
			default:
				return false;
			}
		}
	}
	return false;
}

bool VM_operation_page(int operation, void *address, void * kpage, bool pinned)
{
	//performs load operation
	if (operation == OP_LOAD)
	{
		lock_acquire(&l[LOCK_LOAD]);

		struct page_struct *page = (struct page_struct *) address;

		//get empty frame
		if (page->physical_address == NULL)
			page->physical_address = VM_get_frame(NULL, NULL, PAL_USER);

		lock_release(&l[LOCK_LOAD]);

		//creates mapping from page to frame
		struct frame_struct *vf = address_to_frame(page->physical_address);
		if (vf == NULL)
			return false;
		lock_acquire(&vf->page_list_lock);
		list_push_back(&vf->shared_pages, &page->frame_elem);
		lock_release(&vf->page_list_lock);

		bool success = true;

		if (page->type == TYPE_FILE)
		{
			lock_acquire(&file_lock);
			file_seek(page->file, page->offset);
			size_t ret = file_read(page->file, page->physical_address,
					page->read_bytes);
			lock_release(&file_lock);

			if (ret != page->read_bytes)
			{
				VM_free_frame(page->physical_address, page->pagedir);
				success = false;
			}
			else
			{
				void *block = page->physical_address + page->read_bytes;
				memset(block, 0, page->zero_bytes);
				success = true;
			}
		}
		else if (page->type == TYPE_ZERO)
			memset(page->physical_address, 0, PGSIZE);
		else
		{
			//Load a page to main memory from the swap area
			size_t i = page->index;
			void *address = page->physical_address;
			size_t offset = 0;
			int blocks_in_one_page = PGSIZE / BLOCK_SECTOR_SIZE;

			lock_acquire(&l[LOCK_SWAP]);
			while (offset < blocks_in_one_page)
			{
				if (bitmap_test(swap_bitmap, i) && i < swap_size)
				{
					void *buffer = address + offset * BLOCK_SECTOR_SIZE;
					block_read(swap_block, i, buffer);
				}
				else
					PANIC("Problem when loading a page from swap to main mem");

				i++;
				offset++;
			}

			//Free the swap frame
			offset = 0;
			i = page->index;
			while (offset < blocks_in_one_page)
			{
				if (bitmap_test(swap_bitmap, i) && i < swap_size)
					bitmap_reset(swap_bitmap, i);
				else
					PANIC("Problem when freeing swap -- OP_FREE");

				i++;
				offset++;
			}
			lock_release(&l[LOCK_SWAP]);
		}

		if (!success)
		{
			VM_pin(false, page->physical_address, true);
			return false;
		}

		pagedir_clear_page(page->pagedir, page->virtual_address);
		bool s = pagedir_set_page(page->pagedir, page->virtual_address,
				page->physical_address, page->writable);
		if (!s)
		{
			ASSERT(false);
			VM_pin(false, page->physical_address, true);
			return false;
		}

		pagedir_set_dirty(page->pagedir, page->virtual_address, false);
		pagedir_set_accessed(page->pagedir, page->virtual_address, true);

		page->loaded = true;
		if (!pinned)
			VM_pin(false, page->physical_address, true);
		return true;
	}
	else if (operation == OP_UNLOAD)
	{
		struct page_struct *page = (struct page_struct *) address;
		lock_acquire(&l[LOCK_UNLOAD]);
		if (page->type == TYPE_FILE
				&& pagedir_is_dirty(page->pagedir, page->virtual_address)
				&& !file_check_write(page->file))
		{
			VM_pin(true, kpage, true);
			lock_acquire(&file_lock);

			file_seek(page->file, page->offset);
			file_write(page->file, kpage, page->read_bytes);
			lock_release(&file_lock);
			VM_pin(false, kpage, true);
		}
		else if (page->type == TYPE_SWAP
				|| pagedir_is_dirty(page->pagedir, page->virtual_address))
		{
			//store the current page to swap
			page->type = TYPE_SWAP;

			//move swap from main memory to swap
			void * addr = kpage;
			int blocks_in_one_page = PGSIZE / BLOCK_SECTOR_SIZE;
			lock_acquire(&l[LOCK_SWAP]);
			size_t index = bitmap_scan_and_flip(swap_bitmap, 0,
					blocks_in_one_page,
					false);

			if (index != BITMAP_ERROR)
			{
				size_t offset = 0;
				size_t i = index;
				while (offset < blocks_in_one_page)
				{
					if (bitmap_test(swap_bitmap, i) && (index < swap_size))
					{
						block_write(swap_block, i,
								addr + offset * BLOCK_SECTOR_SIZE);
						i++;
						offset++;
					}
					else
						PANIC("Problem when moving a page from memory to swap");
				}
			}
			else
			{
				PANIC(
						"Problem when moving a page from memory to swap -- index");
			}
			lock_release(&l[LOCK_SWAP]);

			page->index = index;
		}
		lock_release(&l[LOCK_UNLOAD]);

		pagedir_clear_page(page->pagedir, page->virtual_address);
		pagedir_op_page(page->pagedir, page->virtual_address, (void *) page);
		page->loaded = false;
		page->physical_address = NULL;
	}
	else if (operation == OP_FIND)
	{
		//cannot be done here because of incomparable return type
	}
	else if (operation == OP_FREE)
	{
		struct page_struct *page = (struct page_struct *) address;
		if (page == NULL)
			return false;

		//free swap data
		if (page->type == TYPE_SWAP && !page->loaded)
		{
			size_t offset = 0;
			size_t i = page->index;
			int blocks_in_one_page = PGSIZE / BLOCK_SECTOR_SIZE;
			lock_acquire(&l[LOCK_SWAP]);
			while (offset < blocks_in_one_page)
			{

				if (bitmap_test(swap_bitmap, i) && i < swap_size)
					bitmap_reset(swap_bitmap, i);
				else
					PANIC("Problem when freeing swap -- OP_FREE");

				i++;
				offset++;
			}
			lock_release(&l[LOCK_SWAP]);
		}

		//clear mappings from thread's pagedir
		pagedir_clear_page(page->pagedir, page->virtual_address);
		free(page);
		return true;
	}
	else
	{
		return false;
	}
	return false;
}

struct page_struct *VM_stack_grow(void *address, bool pin)
{
	struct page_struct *page = NULL;
	page = VM_new_page(TYPE_ZERO, address, true, NULL,
	NULL, NULL, NULL, NULL);
	if (page != NULL)
	{
		bool success = VM_operation_page(OP_LOAD, page, page->physical_address,
				pin);
		if (success)
			return page;
		else
			return NULL;
	}
	return NULL;
}

struct page_struct *VM_find_page(void *address)
{
	uint32_t *pagedir = NULL;
	pagedir = thread_current()->pagedir;
	if (pagedir != NULL)
	{
		struct page_struct *page = NULL;

		page = (struct page_struct *) pagedir_op_page(pagedir,
				(const void *) address, NULL);

		return page;
	}
	return NULL;
}

unsigned frame_hash(const struct hash_elem *f_, void *aux UNUSED)
{
	const struct frame_struct *f = hash_entry(f_, struct frame_struct,
			hash_elem);
	return hash_int((unsigned) f->physical_address);
}

bool frame_less_helper(const struct hash_elem *a_, const struct hash_elem *b_,
		void *aux UNUSED)
{
	const struct frame_struct *a = hash_entry(a_, struct frame_struct,
			hash_elem);
	const struct frame_struct *b = hash_entry(b_, struct frame_struct,
			hash_elem);

	return a->physical_address < b->physical_address;
}

unsigned mmap_hash(const struct hash_elem *mf_, void *aux UNUSED)
{
	const struct mmap_struct *mf = hash_entry(mf_, struct mmap_struct,
			frame_hash_elem);
	return hash_int((unsigned) mf->mapid);
}

bool mmap_less_helper(const struct hash_elem *a_, const struct hash_elem *b_,
		void *aux UNUSED)
{
	const struct mmap_struct *a = hash_entry(a_, struct mmap_struct,
			frame_hash_elem);
	const struct mmap_struct *b = hash_entry(b_, struct mmap_struct,
			frame_hash_elem);

	return a->mapid < b->mapid;
}
