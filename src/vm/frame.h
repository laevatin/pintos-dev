#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/hash.h"
#include "threads/palloc.h"

void frame_init (void);
void *frame_get_page (void *uaddr, enum palloc_flags flags);
void frame_free_page (void *kaddr);

#endif