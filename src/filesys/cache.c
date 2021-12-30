#include <string.h>
#include "filesys/filesys.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "list.h"
#include "hash.h"

#define CACHE_SIZE 64

struct cache_entry 
{
  char *cache_block;
  // char cache_block[BLOCK_SECTOR_SIZE];
  block_sector_t cache_sector;

  bool is_dirty;
  struct list_elem listelem;
  struct hash_elem hashelem;
};

static struct lock cache_lock;
static struct list cache_list;
static struct hash cache_hash;

static void update_lru (struct cache_entry *entry);
static struct hash_elem *get_cache_elem (block_sector_t sector);
static struct cache_entry *read_to_cache (block_sector_t sector);
static struct cache_entry *cache_evict (void);

static unsigned 
cache_hash_func (const struct hash_elem *e, void *aux UNUSED) 
{
  struct cache_entry *entry = hash_entry(e, struct cache_entry, hashelem);
  return entry->cache_sector;
}

static bool 
cache_hash_less_func (const struct hash_elem *e1, const struct hash_elem *e2,
                                 void *aux UNUSED) 
{
  struct cache_entry *entry1 = hash_entry(e1, struct cache_entry, hashelem);
  struct cache_entry *entry2 = hash_entry(e2, struct cache_entry, hashelem);
  return entry1->cache_sector < entry2->cache_sector;
}

void 
cache_init (void)
{
  lock_init (&cache_lock);
  hash_init (&cache_hash, cache_hash_func, cache_hash_less_func, NULL);
  list_init (&cache_list);
}

void 
cache_flush (void)
{
  lock_acquire (&cache_lock);
  struct list_elem *e = list_begin(&cache_list);

  for (; e != list_end(&cache_list); e = list_next(e)) 
    {
      struct cache_entry *entry = list_entry (e, struct cache_entry, listelem);
      if (entry->is_dirty) 
        block_write(fs_device, entry->cache_sector, entry->cache_block);

      entry->is_dirty = false;
    }

  lock_release (&cache_lock);
}

void 
cache_clear (void)
{
  lock_acquire (&cache_lock);

  while (!list_empty (&cache_list))
    {
      struct list_elem *e = list_pop_front (&cache_list);
      struct cache_entry *entry = list_entry (e, struct cache_entry, listelem);

      if (entry->is_dirty) 
        block_write(fs_device, entry->cache_sector, entry->cache_block);
      
      free (entry->cache_block);
      free (entry);
    }

  lock_init (&cache_lock);
  hash_init (&cache_hash, cache_hash_func, cache_hash_less_func, NULL);
}

void 
cache_block_read (block_sector_t sector, void *buffer, int sector_ofs, int size)
{  
  struct cache_entry *entry;
  lock_acquire (&cache_lock);
  struct hash_elem *elem = get_cache_elem (sector);

  ASSERT (sector_ofs + size <= BLOCK_SECTOR_SIZE);

  if (!elem)
    entry = read_to_cache (sector);
  else 
    {
      entry = hash_entry (elem, struct cache_entry, hashelem);
      update_lru (entry);
    }
  
  memcpy (buffer, entry->cache_block + sector_ofs, size);

  lock_release (&cache_lock);
}

void 
cache_block_write (block_sector_t sector, const void *buffer, int sector_ofs, int size)
{
  struct cache_entry *entry;
  lock_acquire (&cache_lock);
  struct hash_elem *elem = get_cache_elem (sector);

  ASSERT (sector_ofs + size <= BLOCK_SECTOR_SIZE);

  if (!elem) 
    entry = read_to_cache (sector);
  else 
    {
      entry = hash_entry (elem, struct cache_entry, hashelem);
      update_lru (entry);
    }
  
  entry->is_dirty = true;
  memcpy (entry->cache_block + sector_ofs, buffer, size);

  lock_release (&cache_lock);
}

static void 
update_lru (struct cache_entry *entry) 
{
  list_remove (&entry->listelem);
  list_push_back (&cache_list, &entry->listelem);
}

static struct hash_elem *
get_cache_elem (block_sector_t sector)
{
  struct cache_entry tmp;
  tmp.cache_sector = sector;
  return hash_find (&cache_hash, &tmp.hashelem);
}

static struct cache_entry *
read_to_cache (block_sector_t sector)
{
  struct cache_entry *entry;
  if (cache_hash.elem_cnt >= CACHE_SIZE) 
    entry = cache_evict ();
  else 
    {
      entry = malloc (sizeof (struct cache_entry));
      entry->cache_block = malloc (BLOCK_SECTOR_SIZE);
    }

  entry->cache_sector = sector;
  entry->is_dirty = false;

  block_read (fs_device, sector, entry->cache_block);
  hash_insert (&cache_hash, &entry->hashelem);
  list_push_back (&cache_list, &entry->listelem);

  return entry;
}

static struct cache_entry *
cache_evict (void)
{
  struct list_elem *elem = list_front (&cache_list);
  struct cache_entry *entry = list_entry (elem, struct cache_entry, listelem);
  
  list_remove (&entry->listelem);
  hash_delete (&cache_hash, &entry->hashelem);

  if (entry->is_dirty)
    block_write (fs_device, entry->cache_sector, entry->cache_block);
  
  return entry;
}
