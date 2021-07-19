#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "threads/palloc.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

static syscall syscall_vec[SYSCALLNUM];

static void exit (int status);
static void check_frame (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  
  syscall_vec[SYS_HALT] = syscall_halt;
  syscall_vec[SYS_EXIT] = syscall_exit;
  syscall_vec[SYS_EXEC] = syscall_exec;
  syscall_vec[SYS_WAIT] = syscall_wait;
  syscall_vec[SYS_CREATE] = syscall_create;
  syscall_vec[SYS_REMOVE] = syscall_remove;
  syscall_vec[SYS_OPEN] = syscall_open;
  syscall_vec[SYS_FILESIZE] = syscall_filesize;
  syscall_vec[SYS_READ] = syscall_read;
  syscall_vec[SYS_WRITE] = syscall_write;
  syscall_vec[SYS_SEEK] = syscall_seek;
  syscall_vec[SYS_TELL] = syscall_tell;
  syscall_vec[SYS_CLOSE] = syscall_close;
}

static void
syscall_handler (struct intr_frame *f) 
{
  printf ("system call!\n");
  int *sp = f->esp;
  
  check_frame (f);
  
  syscall_vec[*sp] (f);
}

static void 
exit (int status)
{
  thread_exit ();
}

static void 
check_frame (struct intr_frame *f)
{
  int *sp = f->esp;
  /* Can it be simplified to !is_user_vaddr (sp + 3) ? */
  if (!is_user_vaddr (sp)
      || !is_user_vaddr (sp + 1)
      || !is_user_vaddr (sp + 2)
      || !is_user_vaddr (sp + 3))
    exit (-1);
  
  if (*sp < 0 || *sp >= SYSCALLNUM)
    exit (-1);

  return;
}

/* System call for exiting the process */
void 
syscall_exit (struct intr_frame *f) 
{
  int *sp = f->esp;
  exit (*(sp + 1));
} 

/* System call for halting the system */
void 
syscall_halt (struct intr_frame *f UNUSED) 
{
  shutdown_power_off ();
} 

/* System call for program execution */
void 
syscall_exec (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_wait (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_create (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_remove (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_open (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_filesize (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_read (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_write (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_seek (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_tell (struct intr_frame *f) 
{

} 

/* System call for */
void 
syscall_close (struct intr_frame *f) 
{

} 
