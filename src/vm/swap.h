#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"

#define SWAP_SECTOR_INIT 0xFFFFFFFF

void swap_init (void);

block_sector_t swap_get_slot (void);
void swap_write (block_sector_t sector, void *addr);
void swap_read (block_sector_t sector, void *addr);

void free_swap_slot (block_sector_t sector);

#endif