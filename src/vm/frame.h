#ifndef VM_FRAME_H
#define VM_FRAME_H

#include "lib/kernel/hash.h"
#include "threads/palloc.h"

void frame_init (void);
void *frame_get_page (void *uaddr, enum palloc_flags flags);
void frame_free_page (void *kaddr);
void *frame_evict_get (enum palloc_flags flags);

void frame_set_locked (void *kaddr);
void frame_set_unlocked (void *kaddr);
struct frame_entry *frame_get_entry (void *kaddr);

#endif