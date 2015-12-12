#ifndef FILESYS_DIRECTORY_H
#define FILESYS_DIRECTORY_H

#include <stdbool.h>
#include <stddef.h>
#include "devices/block.h"

/* Maximum length of a file name component.
 This is the traditional UNIX maximum length.
 After directories are implemented, this maximum length may be
 retained, but much longer full path names must be allowed. */
#define NAME_MAX 14

#define DIROP_EMPTY 0
#define DIROP_ROOT 1
#define DIROP_GETPAR 2
#define DIROP_ADDPAR 3

struct inode;

/* Opening and closing directories. */
bool dir_create(block_sector_t sector, size_t entry_cnt);
struct dir *dir_open(struct inode *);
struct dir *dir_open_root(void);
struct dir *dir_reopen(struct dir *);
void dir_close(struct dir *);
struct inode *dir_get_inode(struct dir *);

/* Reading and writing. */
bool dir_lookup(const struct dir *, const char *name, struct inode **);
bool dir_add(struct dir *, const char *name, block_sector_t);
bool dir_remove(struct dir *, const char *name);
bool dir_readdir(struct dir *, char name[NAME_MAX + 1]);

#ifdef P4FILESYS
bool dir_op(int choice, struct dir *dir, struct inode **inode,
		block_sector_t sector);
bool dir_remove_validate(struct inode *inode);
#endif

#endif /* filesys/directory.h */
