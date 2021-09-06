#ifndef VM_SWAP_H
#define VM_SWAP_H

#include "devices/block.h"

void swap_init (void);

block_sector_t write_to_swap (void *uaddr);
void read_from_swap (block_sector_t sector, void *addr);
void free_swap_slot (block_sector_t sector);


#endif