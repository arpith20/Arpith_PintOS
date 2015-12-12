#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

tid_t process_execute(const char *file_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(void);

#define RET_STATUS_ERROR -1
#define RET_STATUS_OK 0
#define MAX_ARGS 4096	//max args size is 4KB, the size of page

#endif /* userprog/process.h */
