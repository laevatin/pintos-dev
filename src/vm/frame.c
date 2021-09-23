#include "vm/frame.h"
#include "vm/page.h"
#include "userprog/pagedir.h"
#include "threads/thread.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/pte.h"
#include <stdio.h>

/* Lock to keep data structures synchonized. 
  Users: frame.c page.c */
struct lock frame_lock;

/* An hash table with key=kaddr, v=frame_entry. */
static struct hash frame_table;

/* A list for clock algorithm */
static struct list frame_list;
static struct list_elem *clock = NULL;

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
    struct list_elem listelem;
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
static void next_clock (void);

/* Initialize the frame table. */
void 
frame_init (void) 
{
  lock_init (&frame_lock);
  hash_init (&frame_table, entry_hash, entry_less, NULL);
  list_init (&frame_list);
}

/* Get a page for the user address uaddr using the frame allocator.
  The frame is locked by default. */
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
  // printf ("%d get frame %p\n", thread_current ()->tid, uaddr);
  
  ASSERT (kaddr); /* Failed when swap is full */
  
  entry = malloc (sizeof (struct frame_entry));
  ASSERT (entry);

  entry->uaddr = uaddr;
  entry->kaddr = kaddr;
  entry->t = thread_current ();
  entry->locked = true;

  hash_insert (&frame_table, &entry->elem);
  list_push_back (&frame_list, &entry->listelem);

  if (!locked_outside)
    lock_release (&frame_lock);

  // printf ("%s get %p\n", thread_current ()->name, kaddr);
  return kaddr;
}

/* Free a page with kernel address kaddr using the frame allocator */
void
frame_free_page (void *kaddr)
{
  struct frame_entry f;
  struct hash_elem *e;
  struct frame_entry *entry;
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

  entry = hash_entry (e, struct frame_entry, elem);
  list_remove (&entry->listelem);
  // printf ("remove: %p\n", entry->uaddr);

  if (&entry->listelem == clock)
    next_clock ();

  palloc_free_page (kaddr);
  free (entry);

  if (!locked_outside)
    lock_release (&frame_lock);
  // printf ("freed %p\n", kaddr);
}

/* Delete a page already freed by palloc with kernel address 
  kaddr using the frame allocator */
void
frame_delete_page (void *kaddr)
{
  struct frame_entry f;
  struct hash_elem *e;
  struct frame_entry *entry;

  ASSERT (pg_ofs (kaddr) == 0);

  f.kaddr = kaddr;
  
   // printf ("lock_acquire");
  /* Remove the hash_elem with the same kaddr */
  e = hash_delete (&frame_table, &f.elem);
  ASSERT (e);

  entry = hash_entry (e, struct frame_entry, elem);
  list_remove (&entry->listelem);
  // printf ("remove: %p\n", entry->uaddr);

  if (&entry->listelem == clock)
    next_clock ();

  // printf("lock release\n");
  free (entry);
  // printf ("%s deleted %p\n", thread_current ()->name, kaddr);
}

/* Evict one frame to swap and get one free page */
void *
frame_evict_get (enum palloc_flags flags)
{
  struct frame_entry *f = frame_select_eviction ();
  // printf ("evicted: %p\n", f->kaddr);
  ASSERT (f);

  ASSERT (supt_set_swap (f->t, f->uaddr))

  // if (!supt_set_swap (f->t, f->uaddr))
  //   return NULL;      /* Failed to set swap */

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
  int count = hash_size (&frame_table) * 2;
  struct frame_entry *entry;
  // printf ("----------------------\n");
  while (count--)
    {
      next_clock ();
      entry = list_entry (clock, struct frame_entry, listelem);
      // printf ("entry: %p\n", entry->uaddr);
      if (entry->locked)
        continue;
      else if (pagedir_is_accessed (entry->t->pagedir, entry->uaddr)) {
        pagedir_set_accessed (entry->t->pagedir, entry->uaddr, false);
        continue;
      }
      return entry;
    }
  
  NOT_REACHED ();
  /* Not reached */
  return NULL;
}

static void 
next_clock ()
{
  if (!clock)
    clock = list_begin (&frame_list);
  else 
    clock = list_next (clock);
  if (clock == list_end (&frame_list))
    clock = list_begin (&frame_list);
}