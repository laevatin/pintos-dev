#include "filesys/filesys.h"
#include <debug.h>
#include <stdio.h>
#include <string.h>
#include "filesys/file.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "filesys/directory.h"
#include "threads/thread.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format (void);

static void 
extract_file_name (char *path, char *file_name) 
{
  int len = strlen (path);
  int start;
  /* Find the start of the file name */
  for (start = len - 1; start >= 0; start -= 1) 
    if (path[start] == '/') 
      break;
  
  /* Copy the file name */
  int idx = 0;
  for (start += 1; start < len; start += 1) 
    {
      file_name[idx] = path[start];
      path[start] = '\0';
      idx += 1;
    }

  ASSERT (idx <= NAME_MAX);
  file_name[idx] = '\0';
}

static struct dir *
open_dir_path (const char *path)
{
  struct dir *dir;
  struct thread *t = thread_current ();
  size_t length = strlen (path) + 1;
  char *copy = (char *)malloc (length);

  strlcpy (copy, path, length);

  /* The first thread cannot initialize the pwd, 
    so it may be NULL */
  if (path[0] == '/' || t->pwd == NULL)
    dir = dir_open_root ();
  else 
    dir = dir_reopen (t->pwd);

  if (!dir_is_valid (dir))
    goto err;
  /* Traversal of the directory tree */
  char *token, *saved;
  for (token = strtok_r (copy, "/", &saved); 
        token != NULL; token = strtok_r (NULL, "/", &saved))
    {
      struct inode *inode;
      if (!dir_lookup (dir, token, &inode)) 
        goto err;

      dir_close (dir);
      dir = dir_open (inode);
      if (!dir_is_valid (dir))
        goto err;
    }
  
  free (copy);
  return dir;
err: 
  free (copy);
  dir_close (dir);
  return NULL;
}

/* Initializes the file system module.
   If FORMAT is true, reformats the file system. */
void
filesys_init (bool format) 
{
  fs_device = block_get_role (BLOCK_FILESYS);
  if (fs_device == NULL)
    PANIC ("No file system device found, can't initialize file system.");

  inode_init ();
  free_map_init ();

  if (format) 
    do_format ();

  free_map_open ();
}

/* Shuts down the file system module, writing any unwritten data
   to disk. */
void
filesys_done (void) 
{
  free_map_close ();
}

/* Creates a file or directory named NAME with 
   the given INITIAL_SIZE. 
   Returns true if successful, false otherwise.
   Fails if a file named NAME already exists,
   or if internal memory allocation fails. */
bool
filesys_create (const char *name, off_t initial_size, bool is_file) 
{
  block_sector_t inode_sector = 0;
  char file_name[NAME_MAX + 1] = {0};
  size_t length = strlen (name) + 1;
  char *copy_name = (char *)malloc (length);
  strlcpy (copy_name, name, length);

  extract_file_name (copy_name, file_name);
  struct dir *dir = open_dir_path (copy_name);

  free (copy_name);

  bool success = (dir != NULL
                  && free_map_allocate (1, &inode_sector)
                  && inode_create (inode_sector, initial_size, is_file)
                  && dir_add (dir, file_name, inode_sector, is_file));

  if (!success && inode_sector != 0) 
    free_map_release (inode_sector, 1);
  dir_close (dir);
  
  if (!success)
    return false;
  return success;
}

/* Opens the file with the given NAME.
   Returns the new file if successful or a null pointer
   otherwise. The open file may be a directory.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
struct file *
filesys_open (const char *name)
{
  struct dir *dir;
  char file_name[NAME_MAX + 1] = {0};
  size_t length = strlen (name) + 1;

  if (length == 1)
    return NULL; 

  char *copy_name = (char *)malloc (length);
  strlcpy (copy_name, name, length);

  extract_file_name (copy_name, file_name);
  dir = open_dir_path (copy_name);
  free (copy_name);
  if (dir)
    {
      struct inode *inode;
      /* open "/" */
      if (file_name[0] == '\0')
        inode = inode_reopen (dir_get_inode (dir));
      else 
        dir_lookup (dir, file_name, &inode);
      dir_close (dir);
      return file_open (inode);
    }

  return NULL;
}

/* Deletes the file named NAME.
   Returns true if successful, false on failure.
   Fails if no file named NAME exists,
   or if an internal memory allocation fails. */
bool
filesys_remove (const char *name) 
{
  bool success = false; 
  char file_name[NAME_MAX + 1] = {0};
  size_t length = strlen (name) + 1;

  char *copy_name = (char *)malloc (length);
  strlcpy (copy_name, name, length);

  extract_file_name (copy_name, file_name);
  struct dir *par_dir = open_dir_path (copy_name);
  struct file *file = filesys_open (name);
  free (copy_name);

  if (file && par_dir)
    {
      if (file_is_directory (file)) 
        {
          if (dir_is_empty (file_get_dir (file)))  
            success = dir_remove (par_dir, file_name);
          else 
            return false;
        }
      else 
        success = dir_remove (par_dir, file_name);
    } 

  file_close (file);
  dir_close (par_dir);
  return success;
}

/* Formats the file system. */
static void
do_format (void)
{
  printf ("Formatting file system...");
  free_map_create ();
  if (!dir_create (ROOT_DIR_SECTOR, 16))
    PANIC ("root directory creation failed");
  free_map_close ();
  printf ("done.\n");
}

/* Change the current working directory to path */
bool
filesys_chdir (const char *path)
{
  struct dir *dir = open_dir_path (path);
  if (dir) 
    {
      struct thread *t = thread_current ();
      dir_close (t->pwd);
      t->pwd = dir;
      return true;
    }
  return false;
}

