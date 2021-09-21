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

/* Lock to keep data structures synchonized. 
  Users: frame.c page.c */
struct lock frame_lock;

/* An hash table with key=kaddr, v=frame_entry. */
static struct hash frame_table;

/* Entry struct of frame table. */
struct frame_entry
  {
    void *kaddr;
    void *uaddr;

    /* Owner thread */
    struct thread *t;

    /* If locked, cannot be evicted */
    bool locked;

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
  random_init (153);
}

/* Get a page for the user address uaddr using the frame allocator */
void *
frame_get_page (void *uaddr, enum palloc_flags flags) 
{
  void *kaddr; 
  struct frame_entry *entry;
  bool locked_outside = true; // ugly

  /* Must allocate from user pool */
  ASSERT (flags & PAL_USER);
  ASSERT (pg_ofs (uaddr) == 0);
  
  if (!lock_held_by_current_thread (&frame_lock))
    {
      lock_acquire (&frame_lock);
      // printf ("lock_acquire");
      locked_outside = false;
    }

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
  entry->locked = false;

  hash_insert (&frame_table, &entry->elem);

  if (!locked_outside)
    {
      lock_release (&frame_lock);
      // printf("lock release\n");
    }

  // printf ("%s get %p\n", thread_current ()->name, kaddr);
  return kaddr;
}

/* Free a page with kernel address kaddr using the frame allocator */
void
frame_free_page (void *kaddr)
{
  struct frame_entry f;
  struct hash_elem *e;
  bool locked_outside = true;

  ASSERT (pg_ofs (kaddr) == 0);

  f.kaddr = kaddr;

  /* When evicting, the lock is already held by current thread */
  if (!lock_held_by_current_thread (&frame_lock))
    {
      locked_outside = false;
      lock_acquire (&frame_lock);
      // printf ("lock_acquire");
    }
  /* Remove the hash_elem with the same kaddr */
  e = hash_delete (&frame_table, &f.elem);
  ASSERT (e);

  palloc_free_page (kaddr);
  free (hash_entry (e, struct frame_entry, elem));
  if (!locked_outside)
    {
      lock_release (&frame_lock);
      // printf("lock release\n");

    }
  // printf ("freed %p\n", kaddr);
}

/* Delete a page already freed by palloc with kernel address 
  kaddr using the frame allocator */
void
frame_delete_page (void *kaddr)
{
  struct frame_entry f;
  struct hash_elem *e;

  ASSERT (pg_ofs (kaddr) == 0);

  f.kaddr = kaddr;
  
   // printf ("lock_acquire");
  /* Remove the hash_elem with the same kaddr */
  e = hash_delete (&frame_table, &f.elem);
  // printf("lock release\n");
  
  ASSERT (e);
  free (hash_entry (e, struct frame_entry, elem));
  // printf ("%s deleted %p\n", thread_current ()->name, kaddr);
}

/* Evict one frame to swap and get one free page */
void *
frame_evict_get (enum palloc_flags flags)
{
  struct frame_entry *f = frame_select_eviction ();
  // printf ("evicted: %p\n", f->kaddr);
  ASSERT (f);

  // ASSERT (supt_set_swap (f->t, f->uaddr))

  if (!supt_set_swap (f->t, f->uaddr))
    return NULL;      /* Failed to set swap */

  return palloc_get_page (flags);
}

/* Get the frame entry at kaddr */
struct frame_entry *
frame_get_entry (void *kaddr)
{
  struct frame_entry tmp;
  struct hash_elem *e;

  ASSERT (pg_ofs (kaddr) == 0);
  tmp.kaddr = kaddr;

  e = hash_find (&frame_table, &tmp.elem);

  if (!e)
    PANIC ("frame_get_entry: %p not found", kaddr);

  return hash_entry (e, struct frame_entry, elem);
}

/* Set the frame at kaddr to locked. LOCK before calling */
void
frame_set_locked (void *kaddr)
{
  frame_get_entry (kaddr)->locked = true;
}

/* Set the frame at kaddr to unlocked. LOCK before calling  */
void
frame_set_unlocked (void *kaddr)
{
  frame_get_entry (kaddr)->locked = false;
}

/* Select a frame to evict */
static struct frame_entry *
frame_select_eviction ()
{
  size_t rnd = random_ulong () % hash_size (&frame_table);
  int count = 10;
  
  struct hash_iterator i;

  while (count--)
    {
      hash_first (&i, &frame_table);
      while (hash_next (&i))
        {
          struct frame_entry *f = hash_entry (hash_cur (&i), 
                                                struct frame_entry, elem);
          // struct supt_entry *entry = supt_look_up (f->t->supt, f->uaddr);
          // ASSERT (entry);
          if (f->locked)
            continue;
          // if (entry->locked)
          //   continue;

          if (rnd-- == 0)
            return f;
        }
    }
  
  NOT_REACHED ();
  /* Not reached */
  return NULL;
}
