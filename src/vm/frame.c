#include "vm/frame.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/pte.h"

/* Lock to keep data structures synchonized. */
static struct lock frame_lock;

/* An hash table with key=kaddr, v=frame_entry. */
static struct hash frame_table;

/* Entry struct of frame table. */
struct frame_entry
  {
    void *kaddr;
    void *uaddr;
    struct hash_elem elem;
  };

/* Hash function for frame_entry, since the kaddr is often different, we
  just use it as the hash value. */
static unsigned 
entry_hash (const struct hash_elem *e, void *aux UNUSED)
{
  struct frame_entry *entry = hash_entry (e, struct frame_entry, elem);
  return (unsigned) entry->kaddr;
}

static bool 
entry_less (const struct hash_elem *a, const struct hash_elem *b, void *aux UNUSED)
{
  struct frame_entry *e1 = hash_entry (a, struct frame_entry, elem);
  struct frame_entry *e2 = hash_entry (b, struct frame_entry, elem);
  return e1->kaddr < e2->kaddr;
}

/* Initialize the frame table. */
void 
frame_init (void) 
{
  lock_init (&frame_lock);
  hash_init (&frame_table, entry_hash, entry_less, NULL);
}

/* Get a page for the user address uaddr using the frame allocator */
void *
frame_get_page (void *uaddr, enum palloc_flags flags) 
{
  void *kaddr; 
  struct frame_entry *entry;

  /* Must allocate from user pool */
  ASSERT (flags & PAL_USER);
  ASSERT (pg_ofs (uaddr) == 0);

  kaddr = palloc_get_page (flags);
  if (!kaddr) 
    {
      /* Evict one frame; */
      return NULL;
    }
  
  entry = malloc (sizeof (struct frame_entry));
  if (!entry)
    {
      /* No space for entry */
      return NULL;
    }

  entry->uaddr = uaddr;
  entry->kaddr = kaddr;
  lock_acquire (&frame_lock);
  hash_insert (&frame_table, &entry->elem);
  lock_release (&frame_lock);

  return kaddr;
}

/* Free a page with kernel address kaddr using the frame allocator */
void
frame_free_page (void *kaddr)
{
  struct frame_entry f;
  struct hash_elem *e;

  ASSERT (pg_ofs (kaddr) == 0);

  f.kaddr = kaddr;
  
  lock_acquire (&frame_lock);
  /* Remove the hash_elem with the same kaddr */
  e = hash_delete (&frame_table, &f.elem);
  lock_release (&frame_lock);
  
  ASSERT (!e);

  palloc_free_page (kaddr);
  free (hash_entry (e, struct frame_entry, elem));
}
