#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <stdbool.h>
#include "filesys/off_t.h"
#include "devices/block.h"
#include <list.h>

//various operations needed to be  performed
#define OP_ISDIR 0
#define OP_PARENT 1
#define OP_OPENCNT 2

//defines data to be stored in inode_disk
#define I0 0
#define I1 1
#define I2 2
#define I3 3
#define PAR 4
#define DIR 5
#define LEN 6
#define SIZE 7

#define MEM_SIZE (115 * sizeof(uint32_t))
char zeros[BLOCK_SECTOR_SIZE];

#define I0_BLOCKS 113
#define MAX_FILESIZE 8512000

/* On-disk inode.
 Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
{
	block_sector_t start; /* First data sector. */
	off_t length; /* File size in bytes. */
	unsigned magic; /* Magic number. */

#ifndef P4FILESYS
	uint32_t unused[125]; /* Not used. */
#else
	uint32_t inode_data[10];
	block_sector_t block[115];
#endif
};

/* In-memory inode. */
struct inode
{
	struct list_elem elem; /* Element in inode list. */
	block_sector_t sector; /* Sector number of disk location. */
	int open_cnt; /* Number of openers. */
	bool removed; /* True if deleted, false otherwise. */
	int deny_write_cnt; /* 0: writes ok, >0: deny writes. */
	struct inode_disk data; /* Inode content. */

#ifdef P4FILESYS
	uint32_t inode_data[10];
	uint32_t block[115];
#endif
};

struct bitmap;

void inode_init(void);
#ifndef P4FILESYS
bool inode_create(block_sector_t, off_t);
#else
bool inode_create(block_sector_t, off_t, bool);
#endif
struct inode *inode_open(block_sector_t);
struct inode *inode_reopen(struct inode *);
block_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(struct inode *);

void inode_expand(struct inode *inode, off_t length, size_t new_data_sectors);

#endif /* filesys/inode.h */
