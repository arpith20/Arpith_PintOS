#include "vm/struct.h"

void *VM_get_frame(void *frame, uint32_t *pagedir, enum palloc_flags flags)
{
	//decide based on parameters
	if (frame == NULL)
	{
		struct frame_struct *vf = NULL;
		void *address = palloc_get_page(flags);

		//if allocation was unsuccessful
		if (address == NULL)
		{
			evict();
			return VM_get_frame(NULL, NULL, flags);
		}

		//otherwise, proceed
		vf = (struct frame_struct *) malloc(sizeof(struct frame_struct));
		if (vf != NULL)
		{
			vf->physical_address = address;
			vf->persistent = true;
			list_init(&vf->shared_pages);
			lock_init(&vf->page_list_lock);

			lock_acquire(&l[LOCK_FRAME]);
			list_push_front(&hash_frame_list, &vf->frame_list_elem);
			hash_insert(&hash_frame, &vf->hash_elem);
			lock_release(&l[LOCK_FRAME]);
		}
		return address;
	}
	else
	{
		struct frame_struct *vf = NULL;
		struct page_struct *page = NULL;
		struct list_elem *elem;

		vf = address_to_frame(frame);
		if (vf != NULL)
		{
			lock_acquire(&vf->page_list_lock);
			elem = list_begin(&vf->shared_pages);
			while (elem != list_end(&vf->shared_pages))
			{
				page = list_entry(elem, struct page_struct, frame_elem);
				if (page->pagedir != pagedir)
				{
					elem = list_next(elem);
					continue;
				}
				break;
			}
			lock_release(&vf->page_list_lock);
		}
		return page;
	}

}

//frees frame and writes data to swap
void VM_free_frame(void *address, uint32_t *pagedir)
{
	lock_acquire(&l[LOCK_EVICT]);
	struct frame_struct *vf = NULL;
	struct page_struct *page = NULL;
	struct list_elem *e;

	vf = address_to_frame(address);
	if (vf == NULL)
	{
		lock_release(&l[LOCK_EVICT]);
		return;
	}

	if (pagedir == NULL)
	{

		lock_acquire(&vf->page_list_lock);
		while (true)
		{
			if (list_empty(&vf->shared_pages))
				break;
			e = list_begin(&vf->shared_pages);
			page = list_entry(e, struct page_struct, frame_elem);
			list_remove(&page->frame_elem);
			VM_operation_page(OP_UNLOAD, page, vf->physical_address, false);
		}
		lock_release(&vf->page_list_lock);
	}
	else
	{

		page = VM_get_frame(address, pagedir, PAL_USER);

		if (page != NULL)
		{
			lock_acquire(&vf->page_list_lock);
			list_remove(&page->frame_elem);
			lock_release(&vf->page_list_lock);
			VM_operation_page(OP_UNLOAD, page, vf->physical_address, false);
		}
	}

	if (!list_empty(&vf->shared_pages))
	{
		lock_release(&l[LOCK_EVICT]);
		return;
	}

	lock_acquire(&l[LOCK_FRAME]);
	hash_delete(&hash_frame, &vf->hash_elem);
	list_remove(&vf->frame_list_elem);
	free(vf);
	lock_release(&l[LOCK_FRAME]);
	palloc_free_page(address);

	lock_release(&l[LOCK_EVICT]);
}

bool eviction_clock(struct frame_struct *f)
{
	struct list_elem *ele = list_begin(&f->shared_pages);
	while (ele != list_end(&f->shared_pages))
	{
		struct page_struct *page = list_entry(ele, struct page_struct,
				frame_elem);
		if (page != NULL)
			if (pagedir_is_accessed(page->pagedir, page->virtual_address))
			{
				pagedir_set_accessed(page->pagedir, page->virtual_address,
				false);
				return false;
			}
		ele = list_next(ele);
	}
	return true;
}

void evict()
{
	lock_acquire(&l[LOCK_EVICT]);
	lock_acquire(&l[LOCK_FRAME]);
	struct frame_struct *frame_to_evict = NULL;
	struct list_elem *e = list_end(&hash_frame_list);
	struct frame_struct *cur_frame = list_entry(e, struct frame_struct,
			frame_list_elem);

	while (true)
	{
		//printf("stuck in while loop");
		cur_frame = list_entry(e, struct frame_struct, frame_list_elem);
		if (cur_frame == NULL)
			PANIC("Something wrong with eviction");

		if (cur_frame->persistent == true || !eviction_clock(cur_frame))
		{
			if (e == NULL || e == list_begin(&hash_frame_list))
				e = list_end(&hash_frame_list);
			else
				e = list_prev(e);
			continue;
		}

		frame_to_evict = cur_frame;
		break;
	}

	lock_release(&l[LOCK_FRAME]);
	lock_release(&l[LOCK_EVICT]);
	VM_free_frame(frame_to_evict->physical_address, NULL);
}

struct frame_struct *address_to_frame(void *address)
{
	struct frame_struct f;
	f.physical_address = address;

	if (f.physical_address != NULL)
	{
		lock_acquire(&l[LOCK_FRAME]);
		struct hash_elem *e;
		e = hash_find(&hash_frame, &f.hash_elem);
		lock_release(&l[LOCK_FRAME]);
		if (e == NULL)
			return NULL;
		else
		{
			e = hash_entry(e, struct frame_struct, hash_elem);
			return e;
		}
	}
	return NULL;
}
