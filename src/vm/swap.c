#include "swap.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"
#include <stdio.h>
#include "threads/thread.h"
#include "threads/synch.h"

#define SLOTSIZE PGSIZE
#define SECTORS_PG (SLOTSIZE / BLOCK_SECTOR_SIZE)

struct swap_struct 
  {
    struct bitmap *swap_used_map;
    struct block *swap_block;
    size_t swap_size;
  };

static struct swap_struct swap;

/* Initialize the swap */
void 
swap_init ()
{
  swap.swap_block = block_get_role (BLOCK_SWAP);
  swap.swap_size = block_size (swap.swap_block) / SECTORS_PG;
  swap.swap_used_map = bitmap_create (swap.swap_size);

  ASSERT (swap.swap_block);
  ASSERT (swap.swap_used_map);
  
  bitmap_set_all(swap.swap_used_map, false);
}

block_sector_t 
swap_get_slot () 
{
  size_t available = bitmap_scan (swap.swap_used_map, 0, 1, false);
  /* No available swap space */
  ASSERT (available != BITMAP_ERROR);
  bitmap_set (swap.swap_used_map, available, true);
  return available * SECTORS_PG;
}

/* Write the page in uaddr to swap partition */
void
swap_write (block_sector_t sector, void *addr)
{
  size_t idx;

  ASSERT (sector != SWAP_SECTOR_INIT);
  ASSERT (pg_ofs (addr) == 0);
  /* Write SECTORS_PG block sectors consecutively */
  for (idx = 0; idx < SECTORS_PG; idx++)
    block_write (swap.swap_block, sector + idx, 
                  (void *)((char *)addr + (BLOCK_SECTOR_SIZE * idx)));
}

/* Read the page in swap partition to addr */
void 
swap_read (block_sector_t sector, void *addr)
{
  size_t idx;

  ASSERT (sector != SWAP_SECTOR_INIT);
  ASSERT (pg_ofs (addr) == 0);
  /* Read SECTORS_PG block sectors consecutively */
  for (idx = 0; idx < SECTORS_PG; idx++)
    block_read (swap.swap_block, sector + idx, 
                  (void *)((char *)addr + (BLOCK_SECTOR_SIZE * idx)));
}

/* free the swap slot */
void 
free_swap_slot (block_sector_t sector) 
{
  size_t available = sector / SECTORS_PG;
  ASSERT (bitmap_test (swap.swap_used_map, available))
  bitmap_set (swap.swap_used_map, available, false);
}

