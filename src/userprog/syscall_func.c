#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "devices/input.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#ifdef VM
#include "vm/struct.h"
#endif

static int new_fid = 2;

#ifdef VM
static int new_mapid = 2;
#endif

void system_call_halt(void)
{
	shutdown_power_off();
}

void system_call_exit(int status)
{
	struct thread *t;
	struct list_elem *e;

	t = thread_current();
	if (lock_held_by_current_thread(&file_lock))
		lock_release(&file_lock);

	while (!list_empty(&t->files))
	{
		e = list_begin(&t->files);
		system_call_close(
		list_entry (e, struct file_struct, thread_file_elem)->fid);
	}

#ifdef VM
	while (true)
	{
		if (list_empty(&t->mmap_files))
		break;
		e = list_begin(&t->mmap_files);
		system_call_munmap(
				list_entry (e, struct mmap_struct, thread_mmap_list)->mapid);
	}
#endif

	t->return_status = status;

	//print this when the process exits
	printf("%s: exit(%d)\n", t->name, t->return_status);

	thread_exit();
}

pid_t system_call_exec(const char *cmd_line)
{
	tid_t tid;
	lock_acquire(&file_lock);
	tid = process_execute(cmd_line);
	lock_release(&file_lock);
	return tid;
}

int system_call_wait(pid_t pid)
{
	return process_wait(pid);
}

bool system_call_create(const char *file, unsigned initial_size)
{
	if (file != NULL)
	{
		lock_acquire(&file_lock);
		bool success = filesys_create(file, initial_size);
		lock_release(&file_lock);
		return success;
	}
	else
		system_call_exit(-1);
	return false;
}

bool system_call_remove(const char *file)
{
	if (file != NULL)
	{
		lock_acquire(&file_lock);
		bool success = filesys_remove(file);
		lock_release(&file_lock);
		return success;
	}
	else
		system_call_exit(-1);
	return false;
}

int system_call_open(const char *file)
{
	if (file != NULL)
	{
		struct file *opened_file;
		struct file_struct *opened_file_struct;

		lock_acquire(&file_lock);
		opened_file = filesys_open(file);
		lock_release(&file_lock);

		if (opened_file == NULL)
			return -1;

		opened_file_struct = (struct file_struct *) malloc(
				sizeof(struct file_struct));
		if (opened_file_struct == NULL)
		{
			file_close(opened_file);
			return -1;
		}

		lock_acquire(&file_lock);
		opened_file_struct->f = opened_file;
		opened_file_struct->fid = new_fid++;
		list_push_back(&thread_current()->files,
				&opened_file_struct->thread_file_elem);
		lock_release(&file_lock);

		return opened_file_struct->fid;
	}
	else
	{
		system_call_exit(-1);
	}
	return -1;
}

int system_call_filesize(int fd)
{
	struct file_struct *f = NULL;

	int size = 0;

	f = fd_to_file(fd);
	if (f == NULL)
		return -1;

	lock_acquire(&file_lock);
	size = file_length(f->f);
	lock_release(&file_lock);

	return size;
}

int system_call_read(int fd, void *buffer, unsigned size)
{
	int ret_val = -1;
	unsigned int offset = 0;
	struct file_struct *f = NULL;
#ifdef VM
	size_t remaining = 0;
	size_t bytes_read = 0;
	size_t left = 0;
	void *address = NULL;
	void *temp_buffer = (void *) buffer;
	ret_val = 0;
	const void *esp = (const void*) param_esp;
#endif

	if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size))
		system_call_exit(-1);

	switch (fd)
	{
	case STDIN_FILENO:
		for (offset = 0; offset < size; ++offset)
			*(uint8_t *) (buffer + offset) = input_getc();
		return size;
	case STDOUT_FILENO:
		return -1;
	default:
		f = fd_to_file(fd);
		if (f == NULL)
			return -1;
#ifndef VM
		lock_acquire(&file_lock);
		ret_val = file_read(f->f, buffer, size);
		lock_release(&file_lock);
		return ret_val;
#else
		for (remaining = size; remaining > 0;
				remaining = remaining - bytes_read)
		{
			bool stack_status = ((uint32_t) temp_buffer > 0)
			&& (temp_buffer >= (esp - 32))
			&& ((PHYS_BASE - pg_round_down(temp_buffer)) <= (1 << 23));

			offset = temp_buffer - pg_round_down(temp_buffer);
			address = temp_buffer - offset;
			struct page_struct *page = VM_find_page(address);

			if (page == NULL)
			if (stack_status)
			page = VM_stack_grow(temp_buffer - offset, true);
			else
			system_call_exit(-1);

			//load the page and pin it
			if (!page->loaded)
			VM_operation_page(OP_LOAD, page, page->physical_address, true);

			left = offset + remaining;
			if (left > PGSIZE)
			bytes_read = remaining - left + PGSIZE;
			else
			bytes_read = remaining;

			lock_acquire(&file_lock);
			ret_val = ret_val + file_read(f->f, temp_buffer, bytes_read);
			lock_release(&file_lock);

			temp_buffer = temp_buffer + bytes_read;
			VM_pin(false, page->physical_address, true);
		}
		return ret_val;

#endif
	}
}

int system_call_write(int fd, const void *buffer, unsigned size)
{
	int ret_val = -1;
	const void *esp = (const void*) param_esp;

	struct file_struct *f = NULL;

	if (!is_user_vaddr(buffer) || !is_user_vaddr(buffer + size))
		system_call_exit(-1);
	switch (fd)
	{
	case STDIN_FILENO:
		return -1;
	case STDOUT_FILENO:
		putbuf(buffer, size);
		return size;
	default:
		f = fd_to_file(fd);
		if (f == NULL)
			return -1;

		lock_acquire(&file_lock);
		ret_val = file_write(f->f, buffer, size);
		lock_release(&file_lock);
		return ret_val;
	}
}

void system_call_seek(int fd, unsigned position)
{
	struct file_struct *f = NULL;

	f = fd_to_file(fd);
	if (f == NULL)
		system_call_exit(-1);

	lock_acquire(&file_lock);
	file_seek(f->f, position);
	lock_release(&file_lock);
}

unsigned system_call_tell(int fd)
{
	struct file_struct *f = NULL;
	unsigned position = 0;

	f = fd_to_file(fd);
	if (f == NULL)
		system_call_exit(-1);

	lock_acquire(&file_lock);
	position = file_tell(f->f);
	lock_release(&file_lock);

	return position;
}

void system_call_close(int fd)
{
	struct file_struct *f = NULL;

	f = fd_to_file(fd);

	if (f == NULL)
		system_call_exit(-1);

	lock_acquire(&file_lock);
	list_remove(&f->thread_file_elem);
	file_close(f->f);
	free(f);
	lock_release(&file_lock);
}

struct file_struct *
fd_to_file(int fid)
{
	struct file_struct *f;
	struct list_elem *e;
	struct thread *t;

	t = thread_current();
	for (e = list_begin(&t->files); e != list_end(&t->files); e = list_next(e))
	{
		f = list_entry(e, struct file_struct, thread_file_elem);
		if (f->fid == fid)
			return f;
	}

	return NULL;
}

#ifdef VM
mapid_t system_call_mmap(int fd, void *address)
{
	ASSERT(fd != STDIN_FILENO || fd != STDOUT_FILENO);

	size_t size;
	struct file *f = NULL;
	size_t start_bytes;
	size_t first_bytes;

	size = system_call_filesize(fd);
	lock_acquire(&file_lock);
	struct file_struct *fs = NULL;
	fs = fd_to_file(fd);
	if (fs != NULL)
	f = file_reopen(fs->f);
	else
	{
		lock_release(&file_lock);
		return -1;
	}
	lock_release(&file_lock);

	if (f == NULL || size <= 0 || address == NULL || address == 0x0
			|| pg_ofs(address) != 0)
	return -1;

	size_t offset = 0;
	void *tmp_addr = address;

	while (size > 0)
	{
		if (size < PGSIZE)
		{
			start_bytes = size;
			first_bytes = PGSIZE - size;
		}
		else
		{
			start_bytes = PGSIZE;
			first_bytes = 0;
		}

		//If a page is already mapped, fail
		struct page_struct * temp = VM_find_page(tmp_addr);
		if (temp == NULL)
		{

			temp = VM_new_page(TYPE_FILE, tmp_addr, true, f, offset,
					start_bytes, first_bytes, -1);

			if (temp != NULL)
			{
				offset += PGSIZE;
				size -= start_bytes;
				tmp_addr += PGSIZE;
			}
			else
			return -1;
		}
		else
		return -1;
	}

	mapid_t mapid = new_mapid++;

	struct mmap_struct *mf = (struct mmap_struct *) malloc(
			sizeof(struct mmap_struct));
	if (mf != NULL)
	{
		lock_acquire(&l[LOCK_MMAP]);
		mf->fid = fd;
		mf->mapid = mapid;
		mf->start_address = address;
		mf->end_address = tmp_addr;

		//Insert file to hashmap
		list_push_front(&thread_current()->mmap_files, &mf->thread_mmap_list);
		hash_insert(&hash_mmap, &mf->frame_hash_elem);

		lock_release(&l[LOCK_MMAP]);
		return mapid;
	}
	else
	return NULL;
}

void system_call_munmap(mapid_t mapid)
{
	struct mmap_struct mm_temp;
	struct mmap_struct *mf = NULL;
	struct hash_elem *e = NULL;
	struct page_struct *page = NULL;

	mm_temp.mapid = mapid;
	e = hash_find(&hash_mmap, &mm_temp.frame_hash_elem);
	if (e != NULL)
	{
		mf = hash_entry(e, struct mmap_struct, frame_hash_elem);
	}
	if (mf != NULL)
	{
		void *end_addr = mf->end_address;
		void *cur_addr = mf->start_address;
		// Given a file, free each page mapped in memory
		while (cur_addr < end_addr)
		{
			page = VM_find_page(cur_addr);
			if (page != NULL)
			{
				if (page->loaded)
				{
					VM_pin(true, page, false);

					if (page->loaded && page->physical_address != NULL)
					VM_free_frame(page->physical_address, page->pagedir);
					else
					PANIC("Problem in system unmap");
				}
				VM_operation_page(OP_FREE, page, NULL, NULL);
				cur_addr += PGSIZE;
				page = NULL;
			}
		}
	}
	else
	system_call_exit(-1);

	//remove the file from hash table
	lock_acquire(&l[LOCK_MMAP]);
	hash_delete(&hash_mmap, &mf->frame_hash_elem);
	list_remove(&mf->thread_mmap_list);
	free(mf);
	lock_release(&l[LOCK_MMAP]);
}
#endif
