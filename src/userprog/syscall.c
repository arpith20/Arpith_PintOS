#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void syscall_handler(struct intr_frame *);

//Implemented system calls
void exit (int status);									//CallNumber: 1
int write(int fd, const void *buffer, unsigned size);	//CallNumber: 9

void syscall_init(void)
{
	intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void syscall_handler(struct intr_frame *f UNUSED)
{
	/*	Original Code
	 printf("system call!\n");
	 thread_exit();
	 */

	int *call_number = f->esp;
	//printf("Call Number: %d\n", *call_number);
	switch (*call_number)
	{
	case 1:
		thread_exit();
		break;
	case 9:	//Write System call
		//printf("System call to write");
		write(*(call_number + 1), *(call_number + 2), *(call_number + 3));
		break;
	}
}

void exit (int status)
{
	thread_current()->ret_status = status;
	thread_exit();
}

int write(int fd, const void *buffer, unsigned size)
{
	putbuf(buffer, size);
	return size;
}
