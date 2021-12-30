#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include "devices/block.h"

void cache_init (void);
void cache_flush (void);
void cache_clear (void);

void cache_block_read (block_sector_t sector, void *buffer, 
                                int sector_ofs, int size);
void cache_block_write (block_sector_t sector, const void *buffer, 
                                int sector_ofs, int size);

#endif