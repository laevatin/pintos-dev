#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "threads/malloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "devices/input.h"
#include "filesys/filesys.h"
#include "filesys/file.h"
#include "filesys/fsutil.h"
#include "filesys/off_t.h"
#include "filesys/directory.h"

#define SYSCALLNUM 15
/* Used in process.c when process exit */
void close_all_file (struct thread *t);
void exit (int status);

void syscall_init (void);

typedef void (*syscall)(struct intr_frame *); 

void syscall_exit (struct intr_frame *);
void syscall_halt (struct intr_frame *);
void syscall_exec (struct intr_frame *);
void syscall_wait (struct intr_frame *);
void syscall_create (struct intr_frame *);
void syscall_remove (struct intr_frame *);
void syscall_open (struct intr_frame *);
void syscall_filesize (struct intr_frame *);
void syscall_read (struct intr_frame *);
void syscall_write (struct intr_frame *);
void syscall_seek (struct intr_frame *);
void syscall_tell (struct intr_frame *);
void syscall_close (struct intr_frame *);
void syscall_mmap (struct intr_frame *);
void syscall_munmap (struct intr_frame *);

#endif /* userprog/syscall.h */
