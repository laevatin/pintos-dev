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

typedef uint32_t (*syscall)(int *); 

uint32_t syscall_exit (int *);
uint32_t syscall_halt (int *);
uint32_t syscall_exec (int *);
uint32_t syscall_wait (int *);
uint32_t syscall_create (int *);
uint32_t syscall_remove (int *);
uint32_t syscall_open (int *);
uint32_t syscall_filesize (int *);
uint32_t syscall_read (int *);
uint32_t syscall_write (int *);
uint32_t syscall_seek (int *);
uint32_t syscall_tell (int *);
uint32_t syscall_close (int *);
uint32_t syscall_mmap (int *);
uint32_t syscall_munmap (int *);

#endif /* userprog/syscall.h */
