#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "lib/user/syscall.h"

void syscall_init(void);

//Implemented system calls
void system_call_halt(void);									 //CallNumber: 0
void system_call_exit(int status);								 //CallNumber: 1
pid_t system_call_exec(const char *cmd_line);					 //CallNumber: 2
int system_call_wait(pid_t pid);								 //CallNumber: 3
bool system_call_create(const char *file, unsigned initial_size);//CallNumber: 4
bool system_call_remove(const char *file);						 //CallNumber: 5
int system_call_open(const char *file);							 //CallNumber: 6
int system_call_filesize(int fd);								 //CallNumber: 7
int system_call_read(int fd, void *buffer, unsigned size);		 //CallNumber: 8
int system_call_write(int fd, const void *buffer, unsigned size);//CallNumber: 9
void system_call_seek(int fd, unsigned position);				 //CallNumber: 10
unsigned system_call_tell(int fd);								 //CallNumber: 11
void system_call_close(int fd);									 //CallNumber: 12

struct file_struct * fd_to_file(int fid);

#endif /* userprog/syscall.h */
