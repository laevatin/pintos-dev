#include "page.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/pte.h"
#include "frame.h"
#include "userprog/pagedir.h"
#include "userprog/exception.h"
#include "filesys/file.h"
#include <stdio.h>

extern struct lock frame_lock;

static void load_file_to_page (struct supt_table *table, void *uaddr);
/* hash functions */
static unsigned 
entry_hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct supt_entry *entry = hash_entry (e, struct supt_entry, elem);
  return (unsigned) entry->uaddr;
}

static bool 
entry_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct supt_entry *e1 = hash_entry (a, struct supt_entry, elem);
  struct supt_entry *e2 = hash_entry (b, struct supt_entry, elem);
  return e1->uaddr < e2->uaddr;
}

static void 
entry_destory (struct hash_elem *e, void *aux UNUSED)
{
  struct supt_entry *entry = hash_entry (e, struct supt_entry, elem);
  if (entry->filefrom)
    free (entry->filefrom);
  free (entry);
}

/* Create a supplemental page table for the process. */
struct supt_table *
supt_create ()
{
  struct supt_table *table = malloc (sizeof (struct supt_table));

  hash_init (&table->supt_hash, entry_hash, entry_less, NULL);
  lock_init (&table->supt_lock);

  return table;
}

/* Destroy the supplemental page table. */
void 
supt_destroy (struct supt_table *table)
{
  struct hash_iterator i;

  ASSERT (table);

  /* Free all the swap slot occupied by the supt */
  lock_acquire (&frame_lock);
  lock_acquire (&table->supt_lock);

  hash_first (&i, &table->supt_hash);
  while (hash_next (&i))
    {
      struct supt_entry *entry = hash_entry (hash_cur (&i), 
                                            struct supt_entry, elem);
      if (entry->state == PG_IN_SWAP)
        {
          free_swap_slot (entry->swap_sector);
          // printf ("free slot from destroy: %d\n", entry->swap_sector / 8);
        }
      else if (entry->state == PG_IN_MEM)
        frame_delete_page (entry->kaddr);
    }

  lock_release (&table->supt_lock);
  lock_release (&frame_lock);

  hash_destroy (&table->supt_hash, entry_destory);
  free (table);
}

/* Install the uaddr to kaddr in the supt.
  state should be PG_ZERO or PG_IN_MEM
  PG_ZERO: lazy load the page with all zeroes. kaddr is ignored
  PG_IN_MEM: The uaddr has already been mapped to the kaddr with 
  a frame in the page table */
bool
supt_install_page (struct supt_table *table, void *uaddr, void *kaddr, 
                      enum page_state state)
{
  struct supt_entry *entry = malloc (sizeof (struct supt_entry));;
  struct hash_elem *e;

  ASSERT (table);
  ASSERT (entry);

  ASSERT (uaddr == pg_round_down (uaddr));
  printf ("%d installed: %p \n", thread_current ()->tid, uaddr);
  lock_acquire (&table->supt_lock);

  entry->uaddr = uaddr;
  entry->swap_sector = -1;
  entry->state = state;
  entry->dirty = false;
  entry->filefrom = NULL;

  if (state == PG_IN_MEM)
    entry->kaddr = kaddr;
  else 
    entry->kaddr = NULL;

  /* Check if the page is present */
  e = hash_find (&table->supt_hash, &entry->elem);

  if (e)
    {
      free (entry);
      return false;
    }

  hash_insert (&table->supt_hash, &entry->elem);
  lock_release (&table->supt_lock);

  // printf ("installed: %p.\n", uaddr);  

  return true;
}

/* Install a page in supt by file map. offset is the file offset
  on the first byte in the page. size is the file length in the 
  page, if it is not equal to PG_SIZE, it is filled with zeroes. */
bool 
supt_install_filemap (struct supt_table *table, void *uaddr, struct file *fl, 
                        off_t offset, off_t size)
{
  struct supt_entry *entry;

  if (!supt_install_page (table, uaddr, NULL, PG_ZERO))
    return false;
  
  lock_acquire (&table->supt_lock);

  entry = supt_look_up (table, uaddr);
  entry->filefrom = malloc (sizeof (struct supt_file));
  entry->state = PG_FILE_MAPPED;
  entry->filefrom->fl = fl;
  entry->filefrom->offset = offset;
  entry->filefrom->size_in_page = size;

  lock_release (&table->supt_lock);
  return true;
}

void 
supt_remove_filemap (struct supt_table *table, void *uaddr, off_t size)
{
  uintptr_t base = (uintptr_t) uaddr;
  /* Delete it from the page table */
  uintptr_t top = (uintptr_t)(uaddr + size);
  lock_acquire (&frame_lock);
  lock_acquire (&table->supt_lock);
  while (base <= top)
    {
      struct supt_entry tmp;
      struct hash_elem *e;
      /* write the memory map region to file system */
      supt_set_swap (thread_current (), (void *)base);

      tmp.uaddr = (void *)base;
      /* delete the entry from the supt */
      e = hash_delete (&table->supt_hash, &tmp.elem);

      /* deallocate resources */
      entry_destory (e, NULL);
      base += PGSIZE;
    }

  lock_release (&table->supt_lock);
  lock_release (&frame_lock);
}

/* Find if the page contains address uaddr is in the supt */
bool 
supt_contains (struct supt_table *table, void *uaddr)
{
  struct supt_entry entry;

  ASSERT (is_user_vaddr (uaddr));
  entry.uaddr = pg_round_down (uaddr);
  
  return !!hash_find (&table->supt_hash, &entry.elem);
}

/* Get the supplemental page table entry of uaddr in table.
  Returns NULL if not find. */
struct supt_entry *
supt_look_up (struct supt_table *table, void *uaddr)
{
  struct supt_entry tmp;
  struct hash_elem *e;

  ASSERT (is_user_vaddr (uaddr));
  tmp.uaddr = pg_round_down (uaddr);

  e = hash_find (&table->supt_hash, &tmp.elem);
  if (e)
    return hash_entry (e, struct supt_entry, elem);

  return NULL;
}


bool 
supt_load_page (struct supt_table *table, void *uaddr)
{
  struct supt_entry tmp;
  struct supt_entry *entry;
  struct hash_elem *e;
  void *kaddr;

  ASSERT (is_user_vaddr (uaddr));
  tmp.uaddr = pg_round_down (uaddr);
  
  e = hash_find (&table->supt_hash, &tmp.elem);

  /* No such page in the page table */
  if (!e) 
    return false;

  /* frame table should be locked */
  lock_acquire (&frame_lock);
  lock_acquire (&table->supt_lock);
  
  entry = hash_entry (e, struct supt_entry, elem);
  printf ("%d load %p \n", thread_current ()->tid, uaddr);
  switch (entry->state)
    {
    case PG_IN_MEM:
      frame_set_locked (entry->kaddr);
      lock_release (&frame_lock);
      lock_release (&table->supt_lock);
      return true;
    case PG_ZERO:
      kaddr = frame_get_page (uaddr, PAL_USER | PAL_ZERO);
      break;
    case PG_IN_SWAP:
      kaddr = frame_get_page (uaddr, PAL_USER);
      read_from_swap (entry->swap_sector, kaddr);
      break;
    case PG_FILE_MAPPED:
      kaddr = frame_get_page (uaddr, PAL_USER | PAL_ZERO);
      /* The kaddr is used in load_file_to_page */
      entry->kaddr = kaddr;
      load_file_to_page (table, uaddr);
      break;
    default:
      NOT_REACHED ();
    }
  
  /* What if the frame is evicted before this? */
  /* Update: there is a bug. Must keep synchronized */
  frame_set_locked (kaddr);

  /* Set the mapping relation to the hardware page table. */
  if (!pagedir_set_page (thread_current ()->pagedir, uaddr, kaddr, true))
    {
      frame_free_page (kaddr);
      lock_release (&frame_lock);
      lock_release (&table->supt_lock);
      return false;
    }

  entry->state = PG_IN_MEM;
  entry->kaddr = kaddr;
  lock_release (&frame_lock);
  lock_release (&table->supt_lock);

  return true;
}

/* Set the page at uaddr in the supt table of thread t to SWAP. 
  The page is freed in the frame allocator. The entry of hardware 
  page table of the thread is cleared. */
/* For synchronization purpose (avoid deadlock), you should hold
  frame_lock before calling this. */
bool
supt_set_swap (struct thread *t, void *uaddr)
{
  struct supt_entry tmp;
  struct supt_entry *entry;
  struct hash_elem *e;
  bool locked_outside = true;

  ASSERT (is_user_vaddr (uaddr));
  tmp.uaddr = pg_round_down (uaddr);

  e = hash_find (&t->supt->supt_hash, &tmp.elem);

  /* No such page in the page table */
  if (!e) 
    return false;

  entry = hash_entry (e, struct supt_entry, elem);

  if (entry->state == PG_IN_SWAP || entry->state == PG_FILE_MAPPED)
    return true;

  /* frame_lock and supt_lock may cause dead lock.
  when a thread is holding the frame_lock to require another thread 
  to evict one page, but the other thread is holding the supt_lock 
  and waiting for frame_lock */

  if (!lock_held_by_current_thread (&t->supt->supt_lock))
    {
      lock_acquire (&t->supt->supt_lock);
      locked_outside = false;
    }

  if (!entry->filefrom)
    {
      entry->state = PG_IN_SWAP;
      /* Here must write the kernel address since the thread pagedir may change */
      entry->swap_sector = write_to_swap (entry->kaddr);
    }
  else 
    {
      struct supt_file *sf = entry->filefrom;
      /* Not used and not the error number */
      entry->swap_sector = 0xFFFFFFFE;
      entry->state = PG_FILE_MAPPED;
      file_write_at (sf->fl, entry->kaddr, sf->size_in_page, sf->offset);
    }

  /* Hard to recover */
  ASSERT (entry->swap_sector != 0xFFFFFFFF);
  // if (entry->swap_sector == 0xFFFFFFFF)
  //   {
  //     entry->state = PG_IN_MEM;
  //     return false;
  //   }
  
  frame_free_page (entry->kaddr);
  pagedir_clear_page (t->pagedir, uaddr);

  if (!locked_outside)
    lock_release (&t->supt->supt_lock);
  
  printf ("%d set to swap %p \n", t->tid, uaddr);

  return true;
}

/* Avoid page fault on writing or reading to file system 
  by load the memory required in advance. */
bool 
supt_preload_mem (struct supt_table *table, void *uaddr, size_t size)
{
  uintptr_t base = (uintptr_t) pg_round_down (uaddr);

  /* Stack growth, need to install new zero page */
  if ((((unsigned)PHYS_BASE) - base <= STACK_SIZE)
                    && (base < ((unsigned)PHYS_BASE)))
    return true;

  while (base <= (uintptr_t)(uaddr + size))
    {
      if (!supt_load_page (table, (void *)base))
        return false;
      base += PGSIZE;
    }

  return true;
}

/* Unlock the memory required by supt_preload_mem. */
void 
supt_unlock_mem (struct supt_table *table, void *uaddr, size_t size)
{
  uintptr_t base = (uintptr_t) pg_round_down (uaddr);
  void *kaddr;

  lock_acquire (&frame_lock);
  
  while (base <= (uintptr_t)(uaddr + size))
    {
      kaddr = supt_look_up (table, (void *)base)->kaddr;
      ASSERT (kaddr);
      frame_set_unlocked (kaddr);
      base += PGSIZE;
    }
  
  lock_release (&frame_lock);
}

/* Check if ANY page starts from uaddr with size is exist. */
bool 
supt_check_exist (struct supt_table *table, void *uaddr, size_t size)
{
  uintptr_t base = (uintptr_t) pg_round_down (uaddr);
  struct supt_entry *entry;

  while (base <= (uintptr_t)(uaddr + size))
    {
      entry = supt_look_up (table, (void *)base);
      if (entry) 
        return true;

      base += PGSIZE;
    }

  return false;
}

/* Load the file associated to the page entry with uaddr to the page */
static void 
load_file_to_page (struct supt_table *table, void *uaddr)
{
  struct supt_entry* entry;
  struct supt_file* sf;

  ASSERT (pg_ofs (uaddr) == 0);
  
  entry = supt_look_up (table, uaddr);
  ASSERT (entry);
  sf = entry->filefrom;

  file_read_at (sf->fl, entry->kaddr, sf->size_in_page, sf->offset);
}