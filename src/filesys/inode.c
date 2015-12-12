#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#ifdef P4FILESYS
#include "threads/malloc.h"
#endif

/* Identifies an inode. */
#define INODE_MAGIC 0x87654321

#ifdef P4FILESYS
struct iblock_struct
{
	uint32_t block[128];
};
#endif

/* Returns the number of sectors to allocate for an inode SIZE
 bytes long. */
static inline size_t bytes_to_sectors(off_t size)
{
	return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* Returns the block device sector that contains byte offset POS
 within INODE.
 Returns -1 if INODE does not contain data for a byte at offset
 POS. */
uint32_t byte_to_sector(const struct inode *inode, off_t pos)
{
	if (!inode)
		PANIC("Error: byte->sector function: inode passed is null!");
	else
	{
#ifndef P4FILESYS
		if (pos >= inode->data.length)
#else
		if (pos > inode->inode_data[LEN])
#endif
			return -1;
		else
		{
#ifdef P4FILESYS
			ASSERT(inode!=NULL);
			ASSERT(pos<MAX_FILESIZE);
			uint32_t indirect[128];
			off_t i0_limit = BLOCK_SECTOR_SIZE * I0_BLOCKS;
			off_t i1_limit = BLOCK_SECTOR_SIZE * (I0_BLOCKS + 128);
			if (pos >= i1_limit)
			{
				block_read(fs_device, inode->block[114], &indirect);
				pos = pos - BLOCK_SECTOR_SIZE * (I0_BLOCKS + 128);
				block_read(fs_device, indirect[pos / (BLOCK_SECTOR_SIZE * 128)],
						&indirect);
				return indirect[(pos % (BLOCK_SECTOR_SIZE * 128))
						/ BLOCK_SECTOR_SIZE];
			}
			if (pos >= i0_limit && pos < i1_limit)
			{
				pos = pos - BLOCK_SECTOR_SIZE * I0_BLOCKS;
				block_read(fs_device,
						inode->block[pos / (BLOCK_SECTOR_SIZE * 128)
								+ I0_BLOCKS], &indirect);
				return indirect[(pos % (BLOCK_SECTOR_SIZE * 128))
						/ BLOCK_SECTOR_SIZE];
			}
			if (pos >= 0 && pos < i0_limit)
				return inode->block[pos / BLOCK_SECTOR_SIZE];
#endif
			return inode->data.start + pos / BLOCK_SECTOR_SIZE;
		}
	}
	return -1;
}

/* List of open inodes, so that opening a single inode twice
 returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void inode_init(void)
{
	list_init(&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 writes the new inode to sector SECTOR on the file system
 device.
 Returns true if successful.
 Returns false if memory or disk allocation fails. */
#ifndef P4FILESYS
bool inode_create(block_sector_t sector, off_t length)
#else
bool inode_create(block_sector_t sector, off_t length, bool is_file)
#endif
{
	struct inode_disk *disk_inode = NULL;
#ifndef P4FILESYS
	bool success = false;

	ASSERT(length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 one sector in size, and you should fix that. */
	ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

	disk_inode = calloc(1, sizeof *disk_inode);
	if (disk_inode != NULL)
	{
		size_t sectors = bytes_to_sectors(length);
		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		if (free_map_allocate(sectors, &disk_inode->start))
		{
			block_write(fs_device, sector, disk_inode);
			if (sectors > 0)
			{
				static char zeros[BLOCK_SECTOR_SIZE];
				size_t i;

				for (i = 0; i < sectors; i++)
				block_write(fs_device, disk_inode->start + i, zeros);
			}
			success = true;
		}
		free(disk_inode);
	}
	return success;
#else
	struct inode inode =
	{ };
	bool success = true;

	ASSERT(length >= 0);

	/* If this assertion fails, the inode structure is not exactly
	 one sector in size, and you should fix that. */
	ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

	disk_inode = calloc(1, sizeof *disk_inode);
	if (disk_inode != NULL)
	{
		// the file size should be less than 2^23, i.e. 8MB
		ASSERT(length <= (1 << 23));

		size_t new_data_sectors = (length / BLOCK_SECTOR_SIZE + 1)
				- DIV_ROUND_UP(inode.inode_data[LEN], BLOCK_SECTOR_SIZE);
		inode_expand(&inode, length, new_data_sectors);
		inode.inode_data[LEN] = length;

		//copy all data from inode to inode_disk
		for (int i = 0; i < 10; i++)
			disk_inode->inode_data[i] = inode.inode_data[i];

		disk_inode->length = length;
		disk_inode->magic = INODE_MAGIC;
		disk_inode->inode_data[DIR] = !is_file;
		disk_inode->inode_data[PAR] = ROOT_DIR_SECTOR;

		void *dest = memcpy(&disk_inode->block, &inode.block, MEM_SIZE);
		if (!dest)
			success = false;
		block_write(fs_device, sector, disk_inode);
		free(disk_inode);
	}
	else
		success = false;
	return success;
#endif
}

/* Reads an inode from SECTOR
 and returns a `struct inode' that contains it.
 Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(block_sector_t sector)
{
	struct list_elem *e;
	struct inode *inode;
#ifdef P4FILESYS
	struct inode_disk inode_d;
#endif

	/* Check whether this inode is already open. */
	for (e = list_begin(&open_inodes); e != list_end(&open_inodes); e =
			list_next(e))
	{
		inode = list_entry(e, struct inode, elem);
		if (inode->sector == sector)
		{
			inode_reopen(inode);
			return inode;
		}
	}

	/* Allocate memory. */
	inode = malloc(sizeof *inode);
	if (inode == NULL)
		return NULL;

	/* Initialize. */
	list_push_front(&open_inodes, &inode->elem);
	inode->sector = sector;
	inode->open_cnt = 1;
	inode->deny_write_cnt = 0;
	inode->removed = false;
#ifndef P4FILESYS
	block_read(fs_device, inode->sector, &inode->data);
#endif
#ifdef P4FILESYS
	block_read(fs_device, inode->sector, &inode_d);
	for (int i = 0; i < 10; i++)
	{
		if (i != LEN)
		{
			inode->inode_data[i] = inode_d.inode_data[i];
		}
		else
			inode->inode_data[LEN] = inode_d.length;
	}
	memcpy(&inode->block, &inode_d.block, MEM_SIZE);
#endif
	return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode)
{
	if (inode != NULL)
		inode->open_cnt++;
	return inode;
}

/* Returns INODE's inode number. */
block_sector_t inode_get_inumber(const struct inode *inode)
{
	return inode->sector;
}

/* Closes INODE and writes it to disk.
 If this was the last reference to INODE, frees its memory.
 If INODE was also a removed inode, frees its blocks. */
void inode_close(struct inode *inode)
{
#ifdef P4FILESYS
	struct inode_disk inode_d;
#endif

	/* Ignore null pointer. */
	if (inode == NULL)
		return;

	/* Release resources if this was the last opener. */
	if (--inode->open_cnt == 0)
	{
		/* Remove from inode list and release lock. */
		list_remove(&inode->elem);

#ifdef P4FILESYS
		if (!inode->removed)
		{
			inode_d.magic = INODE_MAGIC;
			for (int i = 0; i < 10; i++)
			{
				if (i != LEN)
					inode_d.inode_data[i] = inode->inode_data[i];
				else
					inode_d.length = inode->inode_data[LEN];
			}
			memcpy(&inode_d.block, &inode->block, MEM_SIZE);
			block_write(fs_device, inode->sector, &inode_d);
			free(inode);
			return;
		}
#endif

		/* Deallocate blocks if removed. */
		if (inode->removed)
		{
			free_map_release(inode->sector, 1);
#ifndef P4FILESYS
			free_map_release(inode->data.start,
					bytes_to_sectors(inode->data.length));
#endif

		}
		free(inode);
	}
}

/* Marks INODE to be deleted when it is closed by the last caller who
 has it open. */
void inode_remove(struct inode *inode)
{
	ASSERT(inode != NULL);
	inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 Returns the number of bytes actually read, which may be less
 than SIZE if an error occurs or end of file is reached. */
off_t inode_read_at(struct inode *inode, void *buffer_, off_t size,
		off_t offset)
{
	uint8_t *buffer = buffer_;
	off_t bytes_read = 0;
#ifndef P4FILESYS
	uint8_t *bounce = NULL;
#endif

	while (size > 0)
	{
		/* Disk sector to read, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually copy out of this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;
#ifndef P4FILESYS
		if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
		{
			/* Read full sector directly into caller's buffer. */
			block_read(fs_device, sector_idx, buffer + bytes_read);
		}
		else
		{
			/* Read sector into bounce buffer, then partially copy
			 into caller's buffer. */
			if (bounce == NULL)
			{
				bounce = malloc(BLOCK_SECTOR_SIZE);
				if (bounce == NULL)
				break;
			}
			block_read(fs_device, sector_idx, bounce);
			memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
		}
#else
		lock_acquire(&buffer_lock);
		struct buffer_struct *bc = NULL;
		int counter = 0;
		for (struct list_elem *e = list_begin(&list_buffer);
				e != list_end(&list_buffer); e = list_next(e))
		{
			struct buffer_struct *bc_temp = list_entry(e, struct buffer_struct,
					buffer_listelem);
			if (sector_idx == bc_temp->sector)
			{
				bc = bc_temp;
				break;
			}
		}

		while (bc == NULL)
		{
			bc = buffer_evict(sector_idx);
			//try 100 times to get the block
			if (counter++ == 100)
				PANIC("Unable to allocate more buffer");
		}

		lock_release(&buffer_lock);

		if (bc == NULL)
			PANIC("In inode_read_at: Buffer cache entry in null");
		else
			bc->access = true;
		void *dest = memcpy((void *) buffer + bytes_read,
				(void *) &bc->content + sector_ofs, (size_t) chunk_size);
		if (!dest)
			PANIC("In inode_read_at: memcpy failed");
#endif

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_read += chunk_size;
	}
#ifndef P4FILESYS
	free(bounce);
#endif
	return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 Returns the number of bytes actually written, which may be
 less than SIZE if end of file is reached or an error occurs.
 (Normally a write at end of file would extend the inode, but
 growth is not yet implemented.) */
off_t inode_write_at(struct inode *inode, const void *buffer_, off_t size,
		off_t offset)
{
	const uint8_t *buffer = buffer_;
	off_t bytes_written = 0;
#ifndef P4FILESYS
	uint8_t *bounce = NULL;
#endif

	if (inode->deny_write_cnt)
		return 0;

#ifdef P4FILESYS
	if (offset + size > inode_length(inode))
	{
		size_t new_data_sectors = ((offset + size) / BLOCK_SECTOR_SIZE + 1)
				- DIV_ROUND_UP(inode->inode_data[LEN], BLOCK_SECTOR_SIZE);
		inode_expand(inode, offset + size, new_data_sectors);
		inode->inode_data[LEN] = offset + size;
	}
#endif

	while (size > 0)
	{
		/* Sector to write, starting byte offset within sector. */
		block_sector_t sector_idx = byte_to_sector(inode, offset);
		int sector_ofs = offset % BLOCK_SECTOR_SIZE;

		/* Bytes left in inode, bytes left in sector, lesser of the two. */
		off_t inode_left = inode_length(inode) - offset;
		int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
		int min_left = inode_left < sector_left ? inode_left : sector_left;

		/* Number of bytes to actually write into this sector. */
		int chunk_size = size < min_left ? size : min_left;
		if (chunk_size <= 0)
			break;

#ifndef P4FILESYS
		if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE)
		{
			/* Write full sector directly to disk. */
			block_write(fs_device, sector_idx, buffer + bytes_written);
		}
		else
		{
			/* We need a bounce buffer. */
			if (bounce == NULL)
			{
				bounce = malloc(BLOCK_SECTOR_SIZE);
				if (bounce == NULL)
				break;
			}

			/* If the sector contains data before or after the chunk
			 we're writing, then we need to read in the sector
			 first.  Otherwise we start with a sector of all zeros. */
			if (sector_ofs > 0 || chunk_size < sector_left)
			block_read(fs_device, sector_idx, bounce);
			else
			memset(bounce, 0, BLOCK_SECTOR_SIZE);
			memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
			block_write(fs_device, sector_idx, bounce);
		}
#else
		lock_acquire(&buffer_lock);
		int counter = 0;
		struct buffer_struct *bc = NULL;
		for (struct list_elem *e = list_begin(&list_buffer);
				e != list_end(&list_buffer); e = list_next(e))
		{
			struct buffer_struct *bc_temp = list_entry(e, struct buffer_struct,
					buffer_listelem);
			if (sector_idx == bc_temp->sector)
			{
				bc = bc_temp;
				break;
			}
		}

		while (bc == NULL)
		{
			bc = buffer_evict(sector_idx);
			//try 100 times to get the block
			if (counter++ == 100)
				PANIC("Unable to allocate more buffer");
		}

		lock_release(&buffer_lock);
		if (bc == NULL)
			PANIC("In inode_write_at: Buffer cache entry in null");
		else
		{
			bc->dirty = true;
			bc->access = true;
		}
		void *dest = memcpy((void *) &bc->content + sector_ofs,
				(void *) buffer + bytes_written, (size_t) chunk_size);
		if (!dest)
			PANIC("In inode_write_at: memcpy failed");
#endif

		/* Advance. */
		size -= chunk_size;
		offset += chunk_size;
		bytes_written += chunk_size;
	}
#ifndef P4FILESYS
	free(bounce);
#endif
	return bytes_written;
}

/* Disables writes to INODE.
 May be called at most once per inode opener. */
void inode_deny_write(struct inode *inode)
{
	inode->deny_write_cnt++;
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 Must be called once by each inode opener who has called
 inode_deny_write() on the inode, before closing the inode. */
void inode_allow_write(struct inode *inode)
{
	ASSERT(inode->deny_write_cnt > 0);
	ASSERT(inode->deny_write_cnt <= inode->open_cnt);
	inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t inode_length(struct inode *inode)
{
#ifndef P4FILESYS
	return inode->data.length;
#else
	return inode->inode_data[LEN];
#endif
}

#ifdef P4FILESYS
//function to allocate more blocks
void inode_expand(struct inode *inode, off_t length, size_t new_data_sectors)
{
#ifndef P4FILESYS
	panic("Wrong invocation of the function");
#endif
	struct iblock_struct l1_block;
	struct iblock_struct l2_block;
	struct iblock_struct l3_block;

	if (new_data_sectors)
	{
		for (; new_data_sectors && inode->inode_data[I0] < I0_BLOCKS;
				inode->inode_data[I0] += 1)
		{
			new_data_sectors -= 1;
			free_map_allocate(1, &inode->block[inode->inode_data[I0]]);
			block_write(fs_device, inode->block[inode->inode_data[I0]], zeros);
		}
		while (new_data_sectors && inode->inode_data[I0] < I0_BLOCKS + 1)
		{
			if (inode->inode_data[I1])
				block_read(fs_device, inode->block[inode->inode_data[I0]],
						&l1_block);
			else
				free_map_allocate(1, &inode->block[inode->inode_data[I0]]);

			for (; new_data_sectors && inode->inode_data[I1] < 128;
					inode->inode_data[I1]++)
			{
				new_data_sectors -= 1;
				free_map_allocate(1, &l1_block.block[inode->inode_data[I1]]);
				block_write(fs_device, l1_block.block[inode->inode_data[I1]],
						zeros);
			}
			block_write(fs_device, inode->block[inode->inode_data[I0]],
					&l1_block);
			if (inode->inode_data[I1] >= 128)
			{
				inode->inode_data[I0] += 1;
				inode->inode_data[I1] = 0;
			}
		}
		while (new_data_sectors && inode->inode_data[I0] <= I0_BLOCKS + 1)
		{

			if (inode->inode_data[I2] || inode->inode_data[I1])
				block_read(fs_device, inode->block[inode->inode_data[I0]],
						&l2_block);
			else
				free_map_allocate(1, &inode->block[inode->inode_data[I0]]);

			while (new_data_sectors && inode->inode_data[I1] < 128)
			{
				if (inode->inode_data[I2])
					block_read(fs_device, l2_block.block[inode->inode_data[I1]],
							&l3_block);
				else
					free_map_allocate(1, &l2_block.block[inode->inode_data[I1]]);

				for (; new_data_sectors && inode->inode_data[I2] < 128;
						inode->inode_data[I2]++)
				{
					new_data_sectors -= 1;
					free_map_allocate(1, &l3_block.block[inode->inode_data[I2]]);
					block_write(fs_device, l3_block.block[inode->inode_data[I2]],
							zeros);
				}
				block_write(fs_device, l2_block.block[inode->inode_data[I1]],
						&l3_block);
				if (inode->inode_data[I2] >= 128)
				{
					inode->inode_data[I1] += 1;
					inode->inode_data[I2] = 0;
				}
			}
			block_write(fs_device, inode->block[inode->inode_data[I0]],
					&l2_block);
		}
	}
	return;
}

uint32_t inode_op(int operation, struct inode * inode)
{
	uint32_t return_val = true;
	switch (operation)
	{
	case OP_ISDIR:
		return_val = inode->inode_data[DIR];
		break;
	case OP_OPENCNT:
		return_val = inode->open_cnt;
		break;
	case OP_PARENT:
		return_val = inode->inode_data[PAR];
		break;
	default:
		return_val = false;
	}
	return return_val;
}
#endif
