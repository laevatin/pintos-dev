#include "swap.h"
#include "threads/palloc.h"
#include "threads/vaddr.h"
#include "threads/synch.h"
#include "lib/kernel/bitmap.h"

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

/* Write the page in uaddr to swap partition */
block_sector_t
write_to_swap (void *uaddr)
{
  block_sector_t start;
  size_t available;
  size_t idx;

  ASSERT (is_user_vaddr (uaddr));
  ASSERT (pg_ofs (uaddr) == 0);

  available = bitmap_scan (swap.swap_used_map, 0, 1, false);

  /* No available swap space */
  if (available == BITMAP_ERROR)
    return -1;

  start = available * SECTORS_PG;

  /* Write SECTORS_PG block sectors consecutively */
  for (idx = 0; idx < SECTORS_PG; idx++)
    block_write (swap.swap_block, start + idx, 
                  (void *)((char *)uaddr + (BLOCK_SECTOR_SIZE * idx)));
  
  bitmap_set (swap.swap_used_map, available, true);

  return start;
}

/* Read the page in swap partition to addr, free the swap slot */
void 
read_from_swap (block_sector_t sector, void *addr)
{
  size_t idx;

  ASSERT (pg_ofs (addr) == 0);

  for (idx = 0; idx < SECTORS_PG; idx++)
    block_read (swap.swap_block, sector + idx, 
                  (void *)((char *)addr + (BLOCK_SECTOR_SIZE * idx)));
  
  free_swap_slot (sector);
}

/* free the swap slot */
void 
free_swap_slot (block_sector_t sector) 
{
  size_t available = sector / SECTORS_PG;
  
  ASSERT (bitmap_test (swap.swap_used_map, available))
  bitmap_set (swap.swap_used_map, available, false);
}

