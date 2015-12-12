#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"

static void syscall_handler(struct intr_frame *);

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
				ret_val = system_call_exec((const char *) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_WAIT:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_wait((pid_t) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_CREATE:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2))
				ret_val = system_call_create((const char *) *(argument + 1),
						(unsigned) *(argument + 2));
			else
				system_call_exit(-1);
			break;
		case SYS_REMOVE:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_remove((const char *) *(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_OPEN:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_open((const char *) *(argument + 1));
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
				ret_val = system_call_read(*(argument + 1),
						(void *) *(argument + 2), (unsigned) *(argument + 3));
			else
				system_call_exit(-1);
			break;
		case SYS_WRITE:	//Write System call
			//printf("System call to write");
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)
					&& is_user_vaddr(argument + 3))
				ret_val = system_call_write(*(argument + 1),
						(const void *) *(argument + 2),
						(unsigned) *(argument + 3));
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
#ifdef VM
			case SYS_MMAP:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2))
			ret_val = system_call_mmap(*(argument + 1), *(argument + 2));
			else
			system_call_exit(-1);
			break;
			case SYS_MUNMAP:
			if (is_user_vaddr(argument + 1))
			system_call_munmap(*(argument + 1));
			else
			system_call_exit(-1);
			break;
#endif
#ifdef P4FILESYS
		case SYS_CHDIR:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_chdir(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_MKDIR:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_mkdir(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_READDIR:
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2))
				ret_val = system_call_readdir(*(argument + 1), *(argument + 2));
			else
				system_call_exit(-1);
			break;
		case SYS_ISDIR:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_isdir(*(argument + 1));
			else
				system_call_exit(-1);
			break;
		case SYS_INUMBER:
			if (is_user_vaddr(argument + 1))
				ret_val = system_call_inumber(*(argument + 1));
			else
				system_call_exit(-1);
			break;
#endif
		default:
			system_call_exit(-1);
		}
	}
	else
		system_call_exit(-1);

#ifdef VM
	param_esp = f->esp;
#endif
	f->eax = ret_val;
}
