#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/pte.h"
#include "lib/random.h"
#include <stdio.h>

/* Lock to keep data structures synchonized. */
static struct lock frame_lock;

/* An hash table with key=kaddr, v=frame_entry. */
static struct hash frame_table;

/* Entry struct of frame table. */
struct frame_entry
  {
    void *kaddr;
    void *uaddr;

    /* Owner thread */
    struct thread *t;

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

static struct frame_entry *frame_select_eviction (void);

/* Initialize the frame table. */
void 
frame_init (void) 
{
  lock_init (&frame_lock);
  hash_init (&frame_table, entry_hash, entry_less, NULL);
  random_init (123);
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
    kaddr = frame_evict_get (flags);

  ASSERT (kaddr); /* Failed when swap is full */
  
  entry = malloc (sizeof (struct frame_entry));
  if (!entry)
    {
      /* No space for entry */
      return NULL;
    }

  entry->uaddr = uaddr;
  entry->kaddr = kaddr;
  entry->t = thread_current ();

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
  
  ASSERT (e);

  palloc_free_page (kaddr);
  free (hash_entry (e, struct frame_entry, elem));
}

/* Evict one frame to swap and get one free page */
void *
frame_evict_get (enum palloc_flags flags)
{
  struct frame_entry *f = frame_select_eviction ();
  uint32_t *pagedir = f->t->pagedir;
  void *oldaddr = f->uaddr;

  // printf ("evicted: %p\n", f->uaddr);
  ASSERT (f);

  if (!supt_set_swap (f->t->supt, oldaddr))
    return NULL;      /* Failed to set swap */
  frame_free_page (f->kaddr);
  pagedir_clear_page (pagedir, oldaddr);

  return palloc_get_page (flags);
}

/* Select a frame to evict */
static struct frame_entry *
frame_select_eviction ()
{
  size_t rnd = random_ulong () % hash_size (&frame_table);
  
  struct hash_iterator i;

  hash_first (&i, &frame_table);
  while (hash_next (&i))
    {
      struct frame_entry *f = hash_entry (hash_cur (&i), struct frame_entry, elem);
      if (rnd-- == 0)
        return f;
    }
  
  /* Not reached */
  return NULL;
}
