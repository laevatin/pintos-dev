#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <string.h>
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/palloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

#define DIRECT_MAP_BLOCKS 100
#define INDIRECT_MAP_BLOCKS 16
#define INDIRECT_SECTOR_NUM 8

/* 8 * 512 / 4 entries per indirect block */
#define INDIRECT_TOTAL_ENTRIES 1024 

/* On-disk inode.
   Must be exactly BLOCK_SECTOR_SIZE bytes long. */
struct inode_disk
  {
    /* Direct blocks for <= 50KB files */
    block_sector_t direct[DIRECT_MAP_BLOCKS];  

    /* Big indirect blocks (4KB target) */       
    block_sector_t big_indirect[INDIRECT_MAP_BLOCKS];    

    uint32_t is_file;                   /* 0 for dir 1 for file */
    off_t length;                       /* File size in bytes. */

    /* The used index of direct blocks */
    uint32_t direct_used;

    /* The used index of indirect blocks */
    uint32_t indirect_used;

    unsigned magic;                     /* Magic number. */
    uint32_t unused[7];
  };

/* Returns the number of sectors to allocate for an inode SIZE
   bytes long. */
static inline size_t
bytes_to_sectors (off_t size)
{
  return DIV_ROUND_UP (size, BLOCK_SECTOR_SIZE);
}

static inline size_t
indirect_block_index (size_t indirect_used)
{
  return indirect_used / INDIRECT_TOTAL_ENTRIES;
}

static inline size_t
indirect_block_offset (size_t indirect_used)
{
  return indirect_used % INDIRECT_TOTAL_ENTRIES;
}

/* read 8 consecutive sectors from fs_device into buffer */
static void 
block_read_big (block_sector_t sector, void *buffer)
{
  for (int i = 0; i < INDIRECT_SECTOR_NUM; i++) 
    cache_block_read (sector + i, 
          (void *)((char *)buffer + (BLOCK_SECTOR_SIZE * i)), 
          0, BLOCK_SECTOR_SIZE);
}

/* write 8 consecutive sectors from fs_device into buffer */
static void 
block_write_big (block_sector_t sector, const void *buffer)
{
  for (int i = 0; i < INDIRECT_SECTOR_NUM; i++) 
    cache_block_write (sector + i, 
          (void *)((char *)buffer + (BLOCK_SECTOR_SIZE * i)), 
          0, BLOCK_SECTOR_SIZE);
}

static block_sector_t
allocate_indirect_blocks (char *zero_mem)
{
  block_sector_t sector;
  /* Allocate indirect blocks */
  if (!free_map_allocate (INDIRECT_SECTOR_NUM, &sector))
    return -1;
  block_write_big (sector, zero_mem);
  return sector;
}

static bool init_inode_disk (struct inode_disk *disk_inode);

static void free_inode_disk (struct inode_disk *inode_disk);

static bool inode_ensure_length(struct inode_disk *d_inode, off_t length);
/* In-memory inode. */
struct inode 
  {
    struct list_elem elem;              /* Element in inode list. */
    block_sector_t sector;              /* Sector number of disk location. */
    int open_cnt;                       /* Number of openers. */
    bool removed;                       /* True if deleted, false otherwise. */
    int deny_write_cnt;                 /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;             /* Inode content. */
  };

/* Returns the block device sector that contains byte offset POS
   within INODE.
   Returns -1 if INODE does not contain data for a byte at offset
   POS. */
static block_sector_t
byte_to_sector (struct inode *inode, off_t pos) 
{
  ASSERT (inode != NULL);
  if (pos >= inode->data.length)
    return -1;

  /* The position is in the direct mapped blocks */
  if (pos < DIRECT_MAP_BLOCKS * BLOCK_SECTOR_SIZE) 
    {
      size_t offset = pos / BLOCK_SECTOR_SIZE;
      return inode->data.direct[offset];
    }

  block_sector_t *indirect_block = palloc_get_page (PAL_ASSERT);
  size_t sector_num = pos / BLOCK_SECTOR_SIZE - DIRECT_MAP_BLOCKS;
  size_t index = indirect_block_index (sector_num);
  block_read_big (inode->data.big_indirect[index], indirect_block);

  block_sector_t in_sector = indirect_block[indirect_block_offset (sector_num)];
  palloc_free_page (indirect_block);
  return in_sector;
}

/* List of open inodes, so that opening a single inode twice
   returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init (void) 
{
  list_init (&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
   writes the new inode to sector SECTOR on the file system
   device.
   Returns true if successful.
   Returns false if memory or disk allocation fails. */
bool
inode_create (block_sector_t sector, off_t length, bool is_file)
{
  struct inode_disk *disk_inode = NULL;
  bool success = false;

  ASSERT (length >= 0);

  /* If this assertion fails, the inode structure is not exactly
     one sector in size, and you should fix that. */
  ASSERT (sizeof *disk_inode == BLOCK_SECTOR_SIZE);

  disk_inode = calloc (1, sizeof *disk_inode);
  if (disk_inode != NULL)
    {
      disk_inode->length = length;
      disk_inode->magic = INODE_MAGIC;
      disk_inode->direct_used = 0;
      disk_inode->indirect_used = 0;
      disk_inode->is_file = (uint32_t)is_file;

      if (init_inode_disk (disk_inode))
        {
          cache_block_write (sector, disk_inode, 0, BLOCK_SECTOR_SIZE);
          success = true;
        }
      free (disk_inode);
    }
  return success;
}

/* Reads an inode from SECTOR
   and returns a `struct inode' that contains it.
   Returns a null pointer if memory allocation fails. */
struct inode *
inode_open (block_sector_t sector)
{
  struct list_elem *e;
  struct inode *inode;

  /* Check whether this inode is already open. */
  for (e = list_begin (&open_inodes); e != list_end (&open_inodes);
       e = list_next (e)) 
    {
      inode = list_entry (e, struct inode, elem);
      if (inode->sector == sector) 
        {
          inode_reopen (inode);
          return inode; 
        }
    }

  /* Allocate memory. */
  inode = malloc (sizeof *inode);
  if (inode == NULL)
    return NULL;

  /* Initialize. */
  list_push_front (&open_inodes, &inode->elem);
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->deny_write_cnt = 0;
  inode->removed = false;
  cache_block_read (inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
  return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen (struct inode *inode)
{
  if (inode != NULL)
    inode->open_cnt++;
  return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber (const struct inode *inode)
{
  return inode->sector;
}

/* Closes INODE and writes it to disk.
   If this was the last reference to INODE, frees its memory.
   If INODE was also a removed inode, frees its blocks. */
void
inode_close (struct inode *inode) 
{
  /* Ignore null pointer. */
  if (inode == NULL)
    return;

  /* Release resources if this was the last opener. */
  if (--inode->open_cnt == 0)
    {
      /* Remove from inode list and release lock. */
      list_remove (&inode->elem);
 
      /* Deallocate blocks if removed. */
      if (inode->removed) 
        {
          free_map_release (inode->sector, 1);
          free_inode_disk (&inode->data);
        }
      free (inode); 
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
   has it open. */
void
inode_remove (struct inode *inode) 
{
  ASSERT (inode != NULL);
  inode->removed = true;
}

/* Whether the inode is removed. */
bool 
inode_is_removed (struct inode *inode)
{
  if (!inode || inode->removed)
    return true;
  return false;
}

/* Is the inode a file? */
bool 
inode_is_file (struct inode *inode)
{
  if (inode->data.is_file)
    return true;
  return false;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
   Returns the number of bytes actually read, which may be less
   than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at (struct inode *inode, void *buffer_, off_t size, off_t offset) 
{
  uint8_t *buffer = buffer_;
  off_t bytes_read = 0;
  uint8_t *bounce = NULL;

  while (size > 0) 
    {
      /* Disk sector to read, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually copy out of this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_block_read (sector_idx, buffer + bytes_read, sector_ofs, chunk_size);      
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_read += chunk_size;
    }
  free (bounce);

  return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
   Returns the number of bytes actually written, which may be
   less than SIZE if end of file is reached or an error occurs.
   (Normally a write at end of file would extend the inode, but
   growth is not yet implemented.) */
off_t
inode_write_at (struct inode *inode, const void *buffer_, off_t size,
                off_t offset) 
{
  const uint8_t *buffer = buffer_;
  off_t bytes_written = 0;
  uint8_t *bounce = NULL;

  if (inode->deny_write_cnt)
    return 0;

  if (offset + size > inode_length (inode))
    {
      if (!inode_ensure_length (&inode->data, offset + size))
        return 0;

      inode->data.length = offset + size;

      cache_block_write (inode->sector, &inode->data, 0, BLOCK_SECTOR_SIZE);
    }

  while (size > 0) 
    {
      /* Sector to write, starting byte offset within sector. */
      block_sector_t sector_idx = byte_to_sector (inode, offset);
      int sector_ofs = offset % BLOCK_SECTOR_SIZE;

      /* Bytes left in inode, bytes left in sector, lesser of the two. */
      off_t inode_left = inode_length (inode) - offset;
      int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
      int min_left = inode_left < sector_left ? inode_left : sector_left;

      /* Number of bytes to actually write into this sector. */
      int chunk_size = size < min_left ? size : min_left;
      if (chunk_size <= 0)
        break;

      cache_block_write (sector_idx, buffer + bytes_written, sector_ofs, chunk_size);
      /* Advance. */
      size -= chunk_size;
      offset += chunk_size;
      bytes_written += chunk_size;
    }
  free (bounce);

  return bytes_written;
}

/* Disables writes to INODE.
   May be called at most once per inode opener. */
void
inode_deny_write (struct inode *inode) 
{
  inode->deny_write_cnt++;
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
   Must be called once by each inode opener who has called
   inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write (struct inode *inode) 
{
  ASSERT (inode->deny_write_cnt > 0);
  ASSERT (inode->deny_write_cnt <= inode->open_cnt);
  inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length (const struct inode *inode)
{
  return inode->data.length;
}

/* Ensure length does not set the inode's length */
static bool
inode_ensure_length(struct inode_disk *d_inode, off_t length)
{
  size_t need = bytes_to_sectors (length) - 
            bytes_to_sectors (d_inode->length);
  size_t need_saved = need;
  block_sector_t indirect_big_sector = -1;

  if (need <= 0)
    return true;
  
  char *zero_mem = palloc_get_page (PAL_ASSERT | PAL_ZERO);
  /* allocate for the sector that direct pointer points to */
  for (; need > 0 && d_inode->direct_used < DIRECT_MAP_BLOCKS; 
          d_inode->direct_used++, need--) 
    {

      if (!free_map_allocate (1, &d_inode->direct[d_inode->direct_used]))
        {
          for (; need < need_saved; need++) 
            {
              d_inode->direct_used -= 1;
              free_map_release (d_inode->direct[d_inode->direct_used], 1);
            }
          palloc_free_page(zero_mem);
          return false;
        }

      cache_block_write (d_inode->direct[d_inode->direct_used], zero_mem, 
                            0, BLOCK_SECTOR_SIZE);
    }
  
  if (need == 0) 
    {
      palloc_free_page(zero_mem);
      return true;
    }

  block_sector_t *indirect_block = palloc_get_page (PAL_ASSERT);
  if (d_inode->indirect_used != 0) 
    {
      indirect_big_sector = d_inode->big_indirect[indirect_block_index (d_inode->indirect_used - 1)];
      block_read_big (indirect_big_sector, indirect_block);
    }

  for (; need > 0 && d_inode->indirect_used < INDIRECT_TOTAL_ENTRIES * INDIRECT_MAP_BLOCKS;
          d_inode->indirect_used++, need--) 
    {
      if (indirect_block_offset (d_inode->indirect_used) == 0) 
        {
          size_t indirect_index = indirect_block_index (d_inode->indirect_used);
          if (indirect_index != 0)
            block_write_big (indirect_big_sector, indirect_block); 

          d_inode->big_indirect[indirect_index] = allocate_indirect_blocks (zero_mem);
          indirect_big_sector = d_inode->big_indirect[indirect_index];
        }

      // TODO: error handling
      /* indirect_block contains the indirect block entries in memory */
      free_map_allocate (1, &indirect_block[indirect_block_offset (d_inode->indirect_used)]);
      cache_block_write (indirect_block[indirect_block_offset (d_inode->indirect_used)], 
                          zero_mem, 0, BLOCK_SECTOR_SIZE);
    }

  block_write_big (indirect_big_sector, indirect_block);

  palloc_free_page(indirect_block);
  palloc_free_page(zero_mem);

  ASSERT (need == 0);
  return true;
}

static bool
init_inode_disk (struct inode_disk *disk_inode)
{
  off_t inode_length = disk_inode->length;
  disk_inode->length = 0;
  if (!inode_ensure_length (disk_inode, inode_length))
    return false;
  disk_inode->length = inode_length;
  return true;
}

static void 
free_inode_disk (struct inode_disk *inode_disk)
{
  size_t direct = inode_disk->direct_used;
  size_t indirect_all = inode_disk->indirect_used;

  for (size_t idx = 0; idx < direct; idx++)
    free_map_release (inode_disk->direct[idx], 1);

  if (indirect_all != 0) 
    {
      size_t indirect_idx = indirect_block_index (indirect_all - 1);
      block_sector_t *indirect_block = palloc_get_page (PAL_ASSERT); 
      for (size_t idx = 0; idx <= indirect_idx; idx++) 
        {
          block_sector_t big_sector = inode_disk->big_indirect[idx];
          size_t remaining = INDIRECT_TOTAL_ENTRIES - 1;
          if (idx == indirect_idx)
            remaining = indirect_block_offset (indirect_all - 1);

          block_read_big (big_sector, indirect_block);
          
          for (size_t off = 0; off <= remaining; off++) 
            free_map_release (indirect_block[off], 1);
          
          free_map_release (big_sector, 8);
        }
      palloc_free_page (indirect_block);
    }
}