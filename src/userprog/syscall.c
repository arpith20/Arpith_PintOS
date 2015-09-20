#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler(struct intr_frame *);

void syscall_init(void)
{
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f)
{
	/*	Original Code
	 printf("system call!\n");
	 thread_exit();
	 */

	int *call_number = f->esp;
	int *argument = f->esp;

	if (is_user_vaddr(call_number))
	{
		//printf("Call Number: %d\n", *call_number);
		switch (*call_number)
		{
		case CALL_HALT:
			halt();
			break;
		case CALL_EXIT:
			if (is_user_vaddr(argument + 1))
				exit(*(argument + 1));
			else
				exit(-1);
			break;
		case CALL_WRITE:	//Write System call
			//printf("System call to write");
			if (is_user_vaddr(argument + 1) && is_user_vaddr(argument + 2)
					&& is_user_vaddr(argument + 3))
				write(*(argument + 1), *(argument + 2), *(argument + 3));
			else
				exit(-1);
			break;
		default:
			exit(-1);
		}
	}
	else
		exit(-1);
}

void halt(void)
{
	shutdown_power_off();
}

void exit(int status)
{
	thread_current()->ret_status = status;
	thread_exit();
}

int write(int fd, const void *buffer, unsigned size)
{
	if (fd == STDIN_FILENO)
		return -1;
	else if (fd == STDOUT_FILENO)
	{
		putbuf(buffer, size);
		return size;
	}
	return -1;
}
