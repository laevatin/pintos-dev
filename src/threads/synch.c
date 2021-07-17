/* This file is derived from source code for the Nachos
   instructional operating system.  The Nachos copyright notice
   is reproduced in full below. */

/* Copyright (c) 1992-1996 The Regents of the University of California.
   All rights reserved.

   Permission to use, copy, modify, and distribute this software
   and its documentation for any purpose, without fee, and
   without written agreement is hereby granted, provided that the
   above copyright notice and the following two paragraphs appear
   in all copies of this software.

   IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO
   ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR
   CONSEQUENTIAL DAMAGES ARISING OUT OF THE USE OF THIS SOFTWARE
   AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF CALIFORNIA
   HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

   THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY
   WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
   WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
   PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS ON AN "AS IS"
   BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
   PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR
   MODIFICATIONS.
*/

#include "threads/synch.h"
#include <stdio.h>
#include <string.h>
#include "threads/interrupt.h"
#include "threads/thread.h"

static void donate_priority (struct thread *, int depth, int prev_priority);
static int get_highest_priority_locks (struct thread *t, int depth);
static struct thread *highest_priority_thread (struct list *thread_list, 
                                                        bool delete);
static void set_lock_highest_acq_priority (struct lock *lock);

/* Initializes semaphore SEMA to VALUE.  A semaphore is a
   nonnegative integer along with two atomic operators for
   manipulating it:

   - down or "P": wait for the value to become positive, then
     decrement it.

   - up or "V": increment the value (and wake up one waiting
     thread, if any). */
void
sema_init (struct semaphore *sema, unsigned value) 
{
  ASSERT (sema != NULL);

  sema->value = value;
  list_init (&sema->waiters);
}

/* Down or "P" operation on a semaphore.  Waits for SEMA's value
   to become positive and then atomically decrements it.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but if it sleeps then the next scheduled
   thread will probably turn interrupts back on. */
void
sema_down (struct semaphore *sema) 
{
  enum intr_level old_level;

  ASSERT (sema != NULL);
  ASSERT (!intr_context ());

  old_level = intr_disable ();
  while (sema->value == 0) 
    {
      list_push_back (&sema->waiters, &thread_current ()->elem);
      thread_block ();
    }
  sema->value--;
  intr_set_level (old_level);
}

/* Down or "P" operation on a semaphore, but only if the
   semaphore is not already 0.  Returns true if the semaphore is
   decremented, false otherwise.

   This function may be called from an interrupt handler. */
bool
sema_try_down (struct semaphore *sema) 
{
  enum intr_level old_level;
  bool success;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (sema->value > 0) 
    {
      sema->value--;
      success = true; 
    }
  else
    success = false;
  intr_set_level (old_level);

  return success;
}

/* Up or "V" operation on a semaphore.  Increments SEMA's value
   and wakes up one thread of those waiting for SEMA, if any.

   This function may be called from an interrupt handler. */
void
sema_up (struct semaphore *sema) 
{
  enum intr_level old_level;
  struct thread *t = NULL;

  ASSERT (sema != NULL);

  old_level = intr_disable ();
  if (!list_empty (&sema->waiters)) 
    thread_unblock (t = highest_priority_thread (&sema->waiters, true));

  sema->value++;
  intr_set_level (old_level);

  if (!intr_context() && is_thread(t) && 
    (thread_get_priority_thread (t) > thread_get_priority ()))
      thread_yield ();
}

static void sema_test_helper (void *sema_);

/* Self-test for semaphores that makes control "ping-pong"
   between a pair of threads.  Insert calls to printf() to see
   what's going on. */
void
sema_self_test (void) 
{
  struct semaphore sema[2];
  int i;

  printf ("Testing semaphores...");
  sema_init (&sema[0], 0);
  sema_init (&sema[1], 0);
  thread_create ("sema-test", PRI_DEFAULT, sema_test_helper, &sema);
  for (i = 0; i < 10; i++) 
    {
      sema_up (&sema[0]);
      sema_down (&sema[1]);
    }
  printf ("done.\n");
}

/* Thread function used by sema_self_test(). */
static void
sema_test_helper (void *sema_) 
{
  struct semaphore *sema = sema_;
  int i;

  for (i = 0; i < 10; i++) 
    {
      sema_down (&sema[0]);
      sema_up (&sema[1]);
    }
}

/* Initializes LOCK.  A lock can be held by at most a single
   thread at any given time.  Our locks are not "recursive", that
   is, it is an error for the thread currently holding a lock to
   try to acquire that lock.

   A lock is a specialization of a semaphore with an initial
   value of 1.  The difference between a lock and such a
   semaphore is twofold.  First, a semaphore can have a value
   greater than 1, but a lock can only be owned by a single
   thread at a time.  Second, a semaphore does not have an owner,
   meaning that one thread can "down" the semaphore and then
   another one "up" it, but with a lock the same thread must both
   acquire and release it.  When these restrictions prove
   onerous, it's a good sign that a semaphore should be used,
   instead of a lock. */
void
lock_init (struct lock *lock)
{
  ASSERT (lock != NULL);
  lock->highest_acq_priority = PRI_MIN;
  lock->holder = NULL;
  sema_init (&lock->semaphore, 1);
}

/* Acquires LOCK, sleeping until it becomes available if
   necessary.  The lock must not already be held by the current
   thread.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
lock_acquire (struct lock *lock)
{
  int acquirepriority;
  struct thread *cur = thread_current ();

  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (!lock_held_by_current_thread (lock));

  /* Priority donation */
  if (lock->holder && !thread_mlfqs)
    {
      acquirepriority = thread_get_priority ();
      if (acquirepriority > lock->highest_acq_priority)
        lock->highest_acq_priority = acquirepriority;

      cur->blockedby = lock->holder;
      donate_priority (cur, PRI_DONATION_LIMIT, PRI_MIN);
    }

  sema_down (&lock->semaphore);

  lock->holder = thread_current ();
  list_push_back (&cur->holdinglocks, &lock->elem);
  cur->blockedby = NULL;
}

/* Tries to acquires LOCK and returns true if successful or false
   on failure.  The lock must not already be held by the current
   thread.

   This function will not sleep, so it may be called within an
   interrupt handler. */
bool
lock_try_acquire (struct lock *lock)
{
  bool success;
  struct thread *t;

  ASSERT (lock != NULL);
  ASSERT (!lock_held_by_current_thread (lock));

  success = sema_try_down (&lock->semaphore);
  if (success) 
    {
      t = thread_current ();
      list_push_back (&t->holdinglocks, &lock->elem);
      lock->holder = t;
    }
    
  return success;
}

/* Releases LOCK, which must be owned by the current thread.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to release a lock within an interrupt
   handler. */
void
lock_release (struct lock *lock) 
{
  struct thread *cur = thread_current ();

  ASSERT (lock != NULL);
  ASSERT (lock_held_by_current_thread (lock));

  
  lock->holder = NULL;
  list_remove (&lock->elem);
  /* Priority donation */
  if (!thread_mlfqs)
    {
      set_lock_highest_acq_priority (lock);
      cur->donatedpriority = get_highest_priority_locks (cur, 
                                          PRI_DONATION_LIMIT);
    }

  sema_up (&lock->semaphore);
}

/* Returns true if the current thread holds LOCK, false
   otherwise.  (Note that testing whether some other thread holds
   a lock would be racy.) */
bool
lock_held_by_current_thread (const struct lock *lock) 
{
  ASSERT (lock != NULL);

  return lock->holder == thread_current ();
}

/* One semaphore in a list. */
struct semaphore_elem 
  {
    struct list_elem elem;              /* List element. */
    struct semaphore semaphore;         /* This semaphore. */
  };

/* Initializes condition variable COND.  A condition variable
   allows one piece of code to signal a condition and cooperating
   code to receive the signal and act upon it. */
void
cond_init (struct condition *cond)
{
  ASSERT (cond != NULL);

  list_init (&cond->waiters);
}

/* Atomically releases LOCK and waits for COND to be signaled by
   some other piece of code.  After COND is signaled, LOCK is
   reacquired before returning.  LOCK must be held before calling
   this function.

   The monitor implemented by this function is "Mesa" style, not
   "Hoare" style, that is, sending and receiving a signal are not
   an atomic operation.  Thus, typically the caller must recheck
   the condition after the wait completes and, if necessary, wait
   again.

   A given condition variable is associated with only a single
   lock, but one lock may be associated with any number of
   condition variables.  That is, there is a one-to-many mapping
   from locks to condition variables.

   This function may sleep, so it must not be called within an
   interrupt handler.  This function may be called with
   interrupts disabled, but interrupts will be turned back on if
   we need to sleep. */
void
cond_wait (struct condition *cond, struct lock *lock) 
{
  struct semaphore_elem waiter;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));
  
  sema_init (&waiter.semaphore, 0);
  list_push_back (&cond->waiters, &waiter.elem);
  lock_release (lock);
  sema_down (&waiter.semaphore);
  lock_acquire (lock);
}

/* If any threads are waiting on COND (protected by LOCK), then
   this function signals one of them to wake up from its wait.
   LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_signal (struct condition *cond, struct lock *lock UNUSED) 
{
  struct list_elem *e;
  struct list_elem *max_e;
  struct semaphore *sema_max_priority;
  int max_priority = PRI_MIN;

  ASSERT (cond != NULL);
  ASSERT (lock != NULL);
  ASSERT (!intr_context ());
  ASSERT (lock_held_by_current_thread (lock));

  /* Get the semaphore with the highest priority waiter */
  if (!list_empty (&cond->waiters)) 
    {
      e = list_begin (&cond->waiters);
      max_e = e;
      sema_max_priority = &list_entry (max_e, 
                              struct semaphore_elem, elem)->semaphore;
      
      for (; e != list_end (&cond->waiters); e = list_next (e))
        {
          struct semaphore *sema = &list_entry (e, 
                                      struct semaphore_elem, elem)->semaphore;
          if (!list_empty (&sema->waiters))
            {
              struct thread *t = 
                      highest_priority_thread (&sema->waiters, false);

              if (thread_get_priority_thread (t) > max_priority)
                {
                  max_e = e;
                  sema_max_priority = sema;
                  max_priority = thread_get_priority_thread (t);
                }
            }
        }

      list_remove (max_e);
      sema_up (sema_max_priority);
    }

}

/* Wakes up all threads, if any, waiting on COND (protected by
   LOCK).  LOCK must be held before calling this function.

   An interrupt handler cannot acquire a lock, so it does not
   make sense to try to signal a condition variable within an
   interrupt handler. */
void
cond_broadcast (struct condition *cond, struct lock *lock) 
{
  ASSERT (cond != NULL);
  ASSERT (lock != NULL);

  while (!list_empty (&cond->waiters))
    cond_signal (cond, lock);
}

/* Donate the priority to the lock holders with search depth `depth' 
  and the previous acquiring thread's priority prev_priority. */
static void 
donate_priority (struct thread *t, int depth, int prev_priority) 
{
  ASSERT (!thread_mlfqs);

  int priority = prev_priority;
  if (depth <= 0 || !is_thread(t)) 
    return;

  t->donatedpriority = get_highest_priority_locks (t, PRI_DONATION_LIMIT);
  /* priority = max(prev_priority, t->donatedpriority, t->priority) */
  if (t->donatedpriority < prev_priority)
    t->donatedpriority = prev_priority;
  else 
    priority = t->donatedpriority;

  if (t->priority > priority)
    priority = t->priority;

  return donate_priority (t->blockedby, depth - 1, priority);
}

/* Get the highest acquiring priority in the holding locks 
  if the thread is not holding any locks, return PRI_MIN. */
static int
get_highest_priority_locks (struct thread *t, int depth)
{
  struct list_elem *e1, *e2;
  int max_donated = PRI_MIN;
  
  if (depth <= 0) 
    return PRI_MIN;

  for (e1 = list_begin (&t->holdinglocks); e1 != list_end (&t->holdinglocks);
    e1 = list_next (e1))
    {
      struct lock *l = list_entry (e1, struct lock, elem);
      if (l->highest_acq_priority > max_donated)
        max_donated = l->highest_acq_priority;
      
      for (e2 = list_begin (&l->semaphore.waiters); 
        e2 != list_end (&l->semaphore.waiters);
        e2 = list_next (e2))
        {
          /* Rather slow */
          struct thread *t1 = list_entry (e2, struct thread, elem);
          int next_prior = get_highest_priority_locks (t1, depth - 1);
          if (next_prior > max_donated) 
            max_donated = next_prior;
        }
    }
  
  return max_donated;
}

/* Pop the thread with highest priority in the given list */
static struct thread *
highest_priority_thread (struct list *thread_list, bool delete)
{
  struct list_elem *e = list_begin (thread_list);
  struct list_elem *max_e = e;
  int max_priority = PRI_MIN;
  struct thread *highest_priority = list_entry (e, struct thread, elem);

  ASSERT (!list_empty (thread_list));

  for (; e != list_end (thread_list); e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, elem);
      int priority = thread_get_priority_thread (t);
      if (priority > max_priority)
        {
          max_priority = priority;
          highest_priority = t;
          max_e = e;
        }
    }
  
  if (delete)
    list_remove (max_e);
  return highest_priority;
}

/* Set a lock's highest acquiring priority */
static void
set_lock_highest_acq_priority (struct lock *lock)
{
  struct list_elem *e;
  int max_priority = PRI_MIN;

  for (e = list_begin (&lock->semaphore.waiters); 
    e != list_end (&lock->semaphore.waiters);
    e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, elem);
      int priority = thread_get_priority_thread (t);
      if (priority > max_priority)
        max_priority = priority;
    }

  lock->highest_acq_priority = max_priority;
}
