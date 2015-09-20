#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);

//Implemented system calls
void halt (void);										//CallNumber: 0
void exit(int status);									//CallNumber: 1
int write(int fd, const void *buffer, unsigned size);	//CallNumber: 9

#define CALL_HALT 0
#define CALL_EXIT 1
#define CALL_EXEC 2
#define CALL_WAIT 3
#define CALL_CREATE 4
#define CALL_REMOVE 5
#define CALL_OPEN 6
#define CALL_FILESIZE 7
#define CALL_READ 8
#define CALL_WRITE 9
#define CALL_SEEK 10
#define CALL_TELL 11
#define CALL_CLOSE 12

#endif /* userprog/syscall.h */
