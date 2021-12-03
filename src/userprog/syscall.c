#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "vm/page.h"
#include "vm/frame.h"

static void syscall_handler (struct intr_frame *f);

static syscall syscall_vec[SYSCALLNUM];

static void check_frame (struct intr_frame *f);
static uint32_t mmap_end (uint32_t val);

/* Here esp must be type of int* */
#define ARG0(esp) (*esp)
#define ARG1(esp) (*(esp + 1))
#define ARG2(esp) (*(esp + 2))
#define ARG3(esp) (*(esp + 3))

/* The lock used when accessing file system 
  used in process.c and syscall.c. */
struct lock file_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
  /* Install the syscall functions. */
  syscall_vec[SYS_HALT    ] = syscall_halt;
  syscall_vec[SYS_EXIT    ] = syscall_exit;
  syscall_vec[SYS_EXEC    ] = syscall_exec;
  syscall_vec[SYS_WAIT    ] = syscall_wait;
  syscall_vec[SYS_CREATE  ] = syscall_create;
  syscall_vec[SYS_REMOVE  ] = syscall_remove;
  syscall_vec[SYS_OPEN    ] = syscall_open;
  syscall_vec[SYS_FILESIZE] = syscall_filesize;
  syscall_vec[SYS_READ    ] = syscall_read;
  syscall_vec[SYS_WRITE   ] = syscall_write;
  syscall_vec[SYS_SEEK    ] = syscall_seek;
  syscall_vec[SYS_TELL    ] = syscall_tell;
  syscall_vec[SYS_CLOSE   ] = syscall_close;
  syscall_vec[SYS_MMAP    ] = syscall_mmap;
  syscall_vec[SYS_MUNMAP  ] = syscall_munmap;
}

/* Entry of system call. */
static void
syscall_handler (struct intr_frame *f) 
{
  thread_current ()->esp = f->esp;
  int *sp = f->esp;
  check_frame (f);
  /* Set the return value. */
  f->eax = syscall_vec[ARG0 (sp)] (sp);
  thread_current ()->esp = NULL;
}

/* Exit the process with return status. */
void 
exit (int status)
{
  struct thread *cur = thread_current ();
  cur->return_status = status;
  thread_exit ();
}

/* Check the address of the parameters on user stack. */
static void 
check_frame (struct intr_frame *f)
{
  int *sp = f->esp;
  if (!is_user_vaddr (sp) || !is_user_vaddr (sp + 3))
    exit (-1);
  
  if (ARG0 (sp) < 0 || ARG0 (sp) >= SYSCALLNUM)
    exit (-1);

  return;
}

/* System call for exiting the process */
uint32_t 
syscall_exit (int *esp) 
{
  exit (ARG1 (esp));
  return 0;
} 

/* System call for halting the system */
uint32_t 
syscall_halt (int *esp UNUSED) 
{
  shutdown_power_off ();
  return 0;
} 

/* System call for program execution */
uint32_t 
syscall_exec (int *esp) 
{
  char *filename = (char *)ARG1 (esp);
  if (!is_user_vaddr (filename) || !filename)
    exit (-1);
  return process_execute (filename);
} 

/* System call for process wait. */
uint32_t 
syscall_wait (int *esp) 
{
  return process_wait ((tid_t)ARG1 (esp));
} 

/* System call for creating a file with initial size. */
uint32_t 
syscall_create (int *esp) 
{
  char *filename = (char *)ARG1 (esp);
  size_t initsize = ARG2 (esp);
  uint32_t retval = 0;

  if (!is_user_vaddr (filename) || !filename)
    exit (-1);

  if (strlen (filename) > NAME_MAX)
    return retval;

  lock_acquire (&file_lock);
  retval = (uint32_t)filesys_create (filename, initsize);
  lock_release (&file_lock);

  return retval;
} 

/* System call for removing the file. */
uint32_t 
syscall_remove (int *esp) 
{
  char *filename = (char *)ARG1 (esp);
  uint32_t retval = 0;

  if (!is_user_vaddr (filename) || !filename)
    exit (-1);

  lock_acquire (&file_lock);
  retval = (uint32_t)filesys_remove (filename);
  lock_release (&file_lock);

  return retval;
} 

/* System call for opening the file in the current thread. */
uint32_t 
syscall_open (int *esp) 
{
  char *filename = (char *)ARG1 (esp);
  struct thread *cur = thread_current ();
  uint32_t retval = -1;

  if (!is_user_vaddr (filename) || !filename)
    exit (-1);
  
  lock_acquire (&file_lock);
  struct file *fl = filesys_open (filename);  
  lock_release (&file_lock);

  if (!fl)
    return retval;

  return thread_add_file (cur, fl);
} 

/* System call for getting the size of the file. */
uint32_t 
syscall_filesize (int *esp) 
{
  int fd = ARG1 (esp);
  uint32_t retval = -1;

  struct file *fl = thread_get_file (thread_current (), fd);
  
  if (!fl)
    return retval;
  
  lock_acquire (&file_lock);
  retval = file_length (fl);
  lock_release (&file_lock);

  return retval;
} 

/* System call for read. File descriptor 0 is STDIN. */
uint32_t 
syscall_read (int *esp) 
{
  int fd = ARG1 (esp);
  char *buffer = (char *)ARG2 (esp);
  uint32_t retval = -1;
  size_t len = ARG3 (esp);
  size_t idx;
  
  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + len))
    exit (-1);
  
  if (fd == 0)
    {
      retval = len;
      for (idx = 0; idx < len; idx++)
        buffer[idx] = input_getc ();
    }
  else 
    {
      struct file *fl = thread_get_file (thread_current (), fd);
      if (!fl)
        return retval;
      lock_acquire (&file_lock);
      if (!supt_preload_mem (thread_current ()->supt, buffer, esp, len))
        exit(-1);
      retval = file_read (fl, buffer, len);
      supt_unlock_mem (thread_current ()->supt, buffer, len);
      lock_release (&file_lock);
    }

  return retval;
} 

/* System call for write. File descriptor 1 is STDOUT. */
uint32_t 
syscall_write (int *esp) 
{
  int fd = ARG1 (esp);
  char *buffer = (char *)ARG2 (esp);
  size_t len = ARG3 (esp);
  uint32_t retval = -1;

  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + len))
    exit (-1);

  if (fd == 1)
    putbuf (buffer, len);
  else 
    {
      struct file *fl = thread_get_file (thread_current (), fd);
      if (!fl)
        return retval;

      lock_acquire (&file_lock);
      if (!supt_preload_mem (thread_current ()->supt, buffer, esp, len))
        exit(-1);
      retval = file_write (fl, buffer, len);
      supt_unlock_mem (thread_current ()->supt, buffer, len);
      lock_release (&file_lock);
    }

  return retval;
} 

/* System call for changing the next byte to be read or written in 
  an opened file fd to position pos. */
uint32_t 
syscall_seek (int *esp) 
{
  int fd = ARG1 (esp);
  size_t pos = ARG2 (esp);

  struct file *fl = thread_get_file (thread_current (), fd);
  if (!fl)
    return 0;
  
  lock_acquire (&file_lock);
  file_seek (fl, pos);
  lock_release (&file_lock);
  return 0;
} 

/* System call for getting the position of next byte to be read 
  or written. */
uint32_t 
syscall_tell (int *esp) 
{
  int fd = ARG1 (esp);
  uint32_t retval = -1;

  struct file *fl = thread_get_file (thread_current (), fd);
  if (!fl)
    return retval;

  lock_acquire (&file_lock);
  retval = file_tell (fl);
  lock_release (&file_lock);

  return retval;
} 

/* System call for closing the opened file. */
uint32_t 
syscall_close (int *esp) 
{
  int fd = ARG1 (esp);
  struct thread *cur = thread_current ();
  
  struct file *fl = thread_get_file (cur, fd);
  if (!fl)
    return 0;

  lock_acquire (&file_lock);
  file_close (fl);
  lock_release (&file_lock);

  thread_remove_file (cur, fd);
  return 0;
} 

/* Close all the files opened by current thread. */
void
close_all_file (struct thread *t)
{
  struct list *filelist = &t->openfds;
  while (!list_empty (filelist))
    {
      struct list_elem *e = list_pop_front (filelist);
      struct filefd *ffd = list_entry (e, struct filefd, elem);

      /* page fault happens when holding the lock */
      if (!lock_held_by_current_thread (&file_lock))
        lock_acquire (&file_lock);
      file_close (ffd->f);
      lock_release (&file_lock);

      free (ffd);
    }
}

/* System call for mapping the file open as fd into addr */
uint32_t
syscall_mmap (int *esp)
{
  int fd = ARG1 (esp);
  uint32_t retval = -1;
  void *addr = (void *) ARG2 (esp);

  struct thread *cur = thread_current ();
  struct file *fl;
  int file_len;

  uintptr_t base = (uintptr_t)addr;
  uintptr_t top;

  if (!addr || fd <= 1 || pg_ofs (addr) != 0)
    return retval;

  fl = thread_get_file (cur, fd);
  lock_acquire (&file_lock);
  /* Reopen the file */
  fl = file_reopen (fl);
  if (!fl) 
    return mmap_end (retval);

  file_len = file_length (fl);
  if (file_len == 0 || supt_check_exist (cur->supt, addr, file_len))
    return mmap_end (retval);
  
  /* Install it to the page table */
  top = (uintptr_t)(addr + file_len);
  while (base <= top)
    {
      off_t size = top - base;
      supt_install_filemap (cur->supt, (void *)base, fl, 
                  base - (uintptr_t)addr, (size >= PGSIZE) ? PGSIZE : size);
      base += PGSIZE;
    }
  
  /* Get the mmapid and add it to current thread */
  retval = thread_add_mmap (cur, fl, addr, file_len);

  return mmap_end (retval);
}

static uint32_t mmap_end (uint32_t val)
{
  lock_release (&file_lock);
  return val;
}

/* Unmaps the mapping designated by mmap */
uint32_t
syscall_munmap (int *esp)
{
  int mapid = ARG1 (esp);
  uint32_t retval = -1;

  off_t file_len;
  struct thread *cur = thread_current ();
  struct file *fl;
  void *addr = thread_munmap (cur, mapid, &file_len, &fl);

  if (!addr)
    return retval;

  supt_remove_filemap (cur->supt, addr, file_len);

  lock_acquire (&file_lock);
  file_close (fl);
  lock_release (&file_lock);
  return 0;
}
