#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"
#include "userprog/process.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "threads/malloc.h"
#include "devices/input.h"

static void syscall_handler(struct intr_frame *);

static struct lock file_lock;
static int new_fid = 2;

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

void syscall_init(void)
{
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");

	lock_init(&file_lock);
}

static void syscall_handler(struct intr_frame *f)
{
	/*	Original Code
	 printf("system call!\n");
	 thread_exit();
	 */

	int *call_number = f->esp;
	int *argument = f->esp;
	int ret_val = 0;

	if (is_user_vaddr(call_number))
	{
		//printf("Call Number: %d\n", *call_number);
		switch (*call_number)
		{
		case SYS_HALT:
			system_call_halt();
			break;
		case SYS_EXIT:
			if (is_user_vaddr(argument + 1))
				system_call_exit(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_EXEC:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_exec((const char *)*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_WAIT:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_wait((pid_t)*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_CREATE:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2))
				ret_val = system_call_create((const char *)*(argument + 1), (unsigned)*(argument + 2));
			else
				system_call_exit(-1);
			break;
		case SYS_REMOVE:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_remove((const char *)*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_OPEN:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_open((const char *)*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_FILESIZE:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_filesize(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_READ:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)
					&& is_user_vaddr(argument + 3))
				ret_val = system_call_read(*(argument + 1), (void *)*(argument + 2),
						(unsigned)*(argument + 3));
			else
				system_call_exit(-1);
			break;
		case SYS_WRITE:	//Write System call
			//printf("System call to write");
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)
					&& is_user_vaddr(argument + 3))
				ret_val = system_call_write(*(argument + 1), (const void *)*(argument + 2),
						(unsigned)*(argument + 3));
			else
				system_call_exit(-1);
			break;
		case SYS_SEEK:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2))
				system_call_seek(*(argument + 1), *(argument + 2));
			else
				system_call_exit(-1);
			break;
		case SYS_TELL:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_tell(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_CLOSE:
			if (is_user_vaddr(argument + 1))
				system_call_close(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		default:
			system_call_exit(-1);
		}
	}
	else
		system_call_exit(-1);

	f->eax = ret_val;
}

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

	t->ret_status = status;
	printf("%s: exit(%d)\n", t->name, t->ret_status);
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
	unsigned int offset;
	struct file_struct *f = NULL;

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

		lock_acquire(&file_lock);
		ret_val = file_read(f->f, buffer, size);
		lock_release(&file_lock);
	}
	return ret_val;
}

int system_call_write(int fd, const void *buffer, unsigned size)
{
	int ret_val = -1;

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
	}
	return ret_val;
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

	lock_acquire(&file_lock);
	list_remove(&f->thread_file_elem);
	file_close(f->f);
	free(f);
	lock_release(&file_lock);
}
