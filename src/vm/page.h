#ifndef VM_PAGE_H
#define VM_PAGE_H

#include "swap.h"
#include "lib/kernel/hash.h"
#include "threads/synch.h"
#include "filesys/file.h"

enum page_state 
  {
    PG_ZERO,
    PG_IN_MEM,
    PG_IN_SWAP,
    PG_FILE_MAPPED,
  };

struct supt_file
  {
    struct file *fl;
    off_t offset;
    off_t size_in_page;
  };

struct supt_entry 
  {
    void *uaddr;
    void *kaddr;

    bool dirty;
    block_sector_t swap_sector;

    enum page_state state;

    /* The memory mapped file description */
    struct supt_file *filefrom;

    struct hash_elem elem;
  };

struct supt_table 
  {
    /* Use hash table as the data structure for supplemental hash table. */
    struct hash supt_hash;

    /* Lock to keep supl synchronized. */
    struct lock supt_lock;
  };

struct supt_table *supt_create (void);
void supt_destroy (struct supt_table *table, uint32_t *pd);
bool supt_install_page (struct supt_table *table, void *uaddr, void *kaddr, 
                          enum page_state state);
bool supt_install_filemap (struct supt_table *table, void *uaddr,  
                        struct file *fl, off_t offset, off_t size);

void supt_remove_filemap (struct supt_table *table, void *uaddr, off_t size);
void supt_delete_entry (struct supt_table *table, void *uaddr);

bool supt_contains (struct supt_table *table, void *uaddr);
struct supt_entry *supt_look_up (struct supt_table *table, void *uaddr);

bool supt_load_page (struct supt_table *table, void *uaddr);
bool supt_set_swap (struct thread *t, void *uaddr);

bool supt_preload_mem (struct supt_table *table, void *uaddr, void *esp, size_t size);
void supt_unlock_mem (struct supt_table *table, void *uaddr, size_t size);

bool supt_check_exist (struct supt_table *table, void *uaddr, size_t size);

#endif