#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "vm/page.h"

static void syscall_handler (struct intr_frame *f);

static syscall syscall_vec[SYSCALLNUM];

static void check_frame (struct intr_frame *f);
static void mmap_end (void);

/* The lock used when accessing file system */
struct lock file_lock;

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  lock_init (&file_lock);
  /* Install the syscall functions. */
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
  
  syscall_vec[SYS_MMAP] = syscall_mmap;
  syscall_vec[SYS_MUNMAP] = syscall_munmap;
}

/* Entry of system call. */
static void
syscall_handler (struct intr_frame *f) 
{
  // printf ("system call!\n");
  int *sp = f->esp;
  thread_current ()->esp = f->esp;
  
  check_frame (f);
  
  syscall_vec[*sp] (f);
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
  // printf ("system call exit\n");
  int *sp = f->esp;
  exit (*(sp + 1));
} 

/* System call for halting the system */
void 
syscall_halt (struct intr_frame *f UNUSED) 
{
  // printf ("system call halt\n");
  shutdown_power_off ();
} 

/* System call for program execution */
void 
syscall_exec (struct intr_frame *f) 
{
  // printf ("system call exec\n");
  int *sp = f->esp;
  char *filename = (char *)(*(sp + 1));
  if (!is_user_vaddr (filename))
    exit (-1);
  f->eax = process_execute (filename);
} 

/* System call for process wait. */
void 
syscall_wait (struct intr_frame *f) 
{
  // printf ("system call wait\n");
  int *sp = f->esp;
  f->eax = process_wait ((tid_t)(*(sp + 1)));
} 

/* System call for creating a file with initial size. */
void 
syscall_create (struct intr_frame *f) 
{
  // printf ("system call create\n");
  int *sp = f->esp;
  char *filename = (char *)(*(sp + 1));
  size_t initsize = *(sp + 2);
  f->eax = 0;

  if (!is_user_vaddr (filename) || !filename)
    exit (-1);

  if (strlen (filename) > NAME_MAX)
    return;

  lock_acquire (&file_lock);
  f->eax = (uint32_t)filesys_create (filename, initsize);
  lock_release (&file_lock);
} 

/* System call for removing the file. */
void 
syscall_remove (struct intr_frame *f) 
{
  // printf ("system call remove\n");
  int *sp = f->esp;
  char *filename = (char *)(*(sp + 1));
  f->eax = 0;

  if (!is_user_vaddr (filename))
    exit (-1);

  lock_acquire (&file_lock);
  f->eax = (uint32_t)filesys_remove (filename);
  lock_release (&file_lock);
} 

/* System call for opening the file in the current thread. */
void 
syscall_open (struct intr_frame *f) 
{
  // printf ("system call open\n");
  int *sp = f->esp;
  char *filename = (char *)(*(sp + 1));
  struct thread *cur = thread_current ();

  f->eax = -1;
  if (!is_user_vaddr (filename) || !filename)
    exit (-1);
  
  lock_acquire (&file_lock);
  struct file *fl = filesys_open (filename);  
  lock_release (&file_lock);

  if (!fl)
    return;

  f->eax = thread_add_file (cur, fl);
} 

/* System call for getting the size of the file. */
void 
syscall_filesize (struct intr_frame *f) 
{
  // printf ("system call filesize\n");
  int *sp = f->esp;
  int fd = *(sp + 1);
  f->eax = -1;

  struct file *fl = thread_get_file (thread_current (), fd);
  if (!fl)
    return;
  
  lock_acquire (&file_lock);
  f->eax = file_length (fl);
  lock_release (&file_lock);
} 

/* System call for read. File descriptor 0 is STDIN. */
void 
syscall_read (struct intr_frame *f) 
{
  // printf ("system call read\n");
  int *sp = f->esp;
  int fd = *(sp + 1);
  char *buffer = (char *)(*(sp + 2));
  size_t len = *(sp + 3);
  size_t idx;

  f->eax = -1;
  
  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + len))
    exit (-1);
  
  if (fd == 0)
    {
      f->eax = len;
      for (idx = 0; idx < len; idx++)
        buffer[idx] = input_getc ();
    }
  else 
    {
      struct file *fl = thread_get_file (thread_current (), fd);
      if (!fl)
        return;
      if (!supt_preload_mem (thread_current ()->supt, buffer, len))
        exit(-1);
      lock_acquire (&file_lock);
      f->eax = file_read (fl, buffer, len);
      lock_release (&file_lock);
      supt_unlock_mem (thread_current ()->supt, buffer, len);

    }
} 

/* System call for write. File descriptor 1 is STDOUT. */
void 
syscall_write (struct intr_frame *f) 
{
  // printf ("system call write\n");
  int *sp = f->esp;
  int fd = *(sp + 1);
  char *buffer = (char *)(*(sp + 2));
  size_t len = *(sp + 3);
  f->eax = -1;

  if (!is_user_vaddr (buffer) || !is_user_vaddr (buffer + len))
    exit (-1);

  if (fd == 1)
    putbuf (buffer, len);
  else 
    {
      struct file *fl = thread_get_file (thread_current (), fd);
      if (!fl)
        return;
      if (!supt_preload_mem (thread_current ()->supt, buffer, len))
        exit(-1);
      lock_acquire (&file_lock);
      f->eax = file_write (fl, buffer, len);
      lock_release (&file_lock);
      supt_unlock_mem (thread_current ()->supt, buffer, len);
    }
} 

/* System call for changing the next byte to be read or written in 
  an opened file fd to position pos. */
void 
syscall_seek (struct intr_frame *f) 
{
  // printf ("system call seek\n");
  int *sp = f->esp;
  int fd = *(sp + 1);
  size_t pos = *(sp + 2);

  struct file *fl = thread_get_file (thread_current (), fd);
  if (!fl)
    return;
  
  lock_acquire (&file_lock);
  file_seek (fl, pos);
  lock_release (&file_lock);
} 

/* System call for getting the position of next byte to be read 
  or written. */
void 
syscall_tell (struct intr_frame *f) 
{
  // printf ("system call tell\n");
  int *sp = f->esp;
  int fd = *(sp + 1);
  f->eax = -1;

  struct file *fl = thread_get_file (thread_current (), fd);
  if (!fl)
    return;

  lock_acquire (&file_lock);
  f->eax = file_tell (fl);
  lock_release (&file_lock);
} 

/* System call for closing the opened file. */
void 
syscall_close (struct intr_frame *f) 
{
  // printf ("system call close\n");
  int *sp = f->esp;
  int fd = *(sp + 1);
  struct thread *cur = thread_current ();
  
  struct file *fl = thread_get_file (cur, fd);
  if (!fl)
    return;

  lock_acquire (&file_lock);
  file_close (fl);
  lock_release (&file_lock);

  thread_remove_file (cur, fd);
} 

/* System call for mapping the file open as fd into addr */
void
syscall_mmap (struct intr_frame *f)
{
  int *sp = f->esp;
  int fd = *(sp + 1);
  
  void *addr = (void *)*(sp + 2);

  struct thread *cur = thread_current ();
  struct file *fl;
  int file_len;

  uintptr_t base = (uintptr_t)addr;
  uintptr_t top;

  f->eax = -1;
  if (fd <= 1 || pg_ofs (addr) != 0)
    return;

  fl = thread_get_file (cur, fd);
  lock_acquire (&file_lock);
  /* Reopen the file */
  fl = file_reopen (fl);
  if (!fl) 
    return mmap_end ();

  file_len = file_length (fl);
  if (file_len == 0 || supt_check_exist (cur->supt, addr, file_len))
    return mmap_end ();
  
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
  f->eax = thread_add_mmap (cur, fl, addr, file_len);

  return mmap_end ();
}

static void mmap_end ()
{
  lock_release (&file_lock);
  return;
}

/* Unmaps the mapping designated by mmap */
void
syscall_munmap (struct intr_frame *f)
{
  int *sp = f->esp;
  int mapid = *(sp + 1);

  off_t file_len;
  struct thread *cur = thread_current ();
  struct file *fl;
  void *addr = thread_munmap (cur, mapid, &file_len, &fl);
 
  uintptr_t top;
  uintptr_t base = (uintptr_t)addr;

  f->eax = -1;
  if (!addr)
    return;
  
  lock_acquire (&file_lock);
  file_close (fl);
  lock_release (&file_lock);

  /* Delete it from the page table */
  top = (uintptr_t)(addr + file_len);
  while (base <= top)
    {
      supt_delete_entry (cur->supt, (void *)base);
      base += PGSIZE;
    }

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

      /* Handle the situation that page fault happens when holding the lock */
      if (!lock_held_by_current_thread (&file_lock))
        lock_acquire (&file_lock);
      file_close (ffd->f);
      lock_release (&file_lock);

      free (ffd);
    }
}