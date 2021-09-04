#include "page.h"
#include "lib/kernel/hash.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/pte.h"
#include "frame.h"
#include "userprog/pagedir.h"

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
  ASSERT (table);

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
    
  entry->uaddr = uaddr;
  entry->swap_sector = -1;
  entry->state = state;
  entry->dirty = false;

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

  lock_acquire (&table->supt_lock);
  hash_insert (&table->supt_hash, &entry->elem);
  lock_release (&table->supt_lock);

  return true;
}

/* Find if the page contains address uaddr is in the supt */
bool 
supt_look_up (struct supt_table *table, void *uaddr)
{
  struct supt_entry entry;

  ASSERT (is_user_vaddr (uaddr));
  entry.uaddr = pg_round_down (uaddr);
  
  return !!hash_find (&table->supt_hash, &entry.elem);
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

  entry = hash_entry (e, struct supt_entry, elem);
  
  ASSERT (entry->state != PG_IN_MEM);

  if (entry->state == PG_ZERO)
    kaddr = frame_get_page (uaddr, PAL_USER | PAL_ZERO);
  
  /* Set the mapping relation to the hardware page table. */
  if (!pagedir_set_page (thread_current ()->pagedir, uaddr, kaddr, true))
    {
      frame_free_page (kaddr);
      return false;
    }

  entry->state = PG_IN_MEM;
  entry->kaddr = kaddr;

  return true;
}
