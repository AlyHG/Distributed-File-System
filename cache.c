#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "cache.h"

static cache_entry_t *cache = NULL;
static int cache_size = 0;
static int clock = 0;
static int num_queries = 0;
static int num_hits = 0;

int cache_create(int num_entries) {
  //Parameter Checks
   if(num_entries < 2 || num_entries > 4096) {
    return -1;
  }
  //If Cache does not exist then allocate memory to cache
  if(cache == NULL) {
    cache = calloc(num_entries, sizeof(cache_entry_t));
    cache_size = num_entries;
    return 1;
  }
  return -1;
}


int cache_destroy(void) {
  //If Cache exists then free memory used by the cache
  if(cache != NULL) {
    free(cache);
    cache = NULL;
    cache_size = 0;
    clock = 0;
    return 1;
  }
  return -1;
}


int cache_lookup(int disk_num, int block_num, uint8_t *buf) {
  //If cache or buffer of invalid size / dont exist
  if(cache == NULL || buf == NULL) {
    return -1;
  }
  //Bounds Check
  if(block_num < 0 || block_num >= 256) {
    return -1;
  }
  if(disk_num < 0 || disk_num >= 16) {
    return -1;
  }
  num_queries += 1;
  int index = 0;
  //Lookup the block identified by disk_num and block_num in the cache. 
  while(index < cache_size) {
    //If found in cache copy from cache to to buffer
    if(cache[index].disk_num == disk_num && cache[index].block_num == block_num && cache[index].valid) {
      memcpy(buf, cache[index].block, 256);
      clock += 1;
      cache[index].access_time = clock;
      num_hits += 1;
      return 1;
    }
    index += 1;
  }
  return -1;
}

void cache_update(int disk_num, int block_num, const uint8_t *buf) {
  //If cacher or buffer of invalid size /Dont exist
  if(cache == NULL || buf == NULL) {
    return;
  }
  //Bounds Check
  if(block_num < 0 || block_num >= 256) {
    return;
  }
  if(disk_num < 0 || disk_num >= 16) {
    return;
  }
  int index = 0;
  //Lookup the block identified by disk_num and block_num in the cache. 
  while(index < cache_size) {
    //If location in cache is found with corresponding block and disk nums, copy from buffer into cache location
    if(cache[index].disk_num == disk_num && cache[index].block_num == block_num && cache[index].valid) {
      memcpy(cache[index].block, buf, 256);
      clock += 1;
      cache[index].access_time = clock;
      return;
    }
    index += 1;
  }
  return;
}

int cache_insert(int disk_num, int block_num, const uint8_t *buf) {
  //If cache or buffer of invalid size / Dont exist
  if(cache == NULL || buf == NULL) {
    return -1;
  }
  //Bounds Check
  if(block_num < 0 || block_num >= 256) {
    return -1;
  }
  if(disk_num < 0 || disk_num >= 16) {
    return -1;
  }
  int index = 0;
  int lru_index = 0;
  while(index < cache_size) {
    //If block entry for block and disk num exist, return -1
    if(cache[index].disk_num == disk_num && cache[index].block_num == block_num && cache[index].valid) {
      return -1;
    }
    //else insert entry into cache for that block and disk num
    if(cache[index].valid == false) {
      memcpy(cache[index].block, buf, 256);
      cache[index].block_num = block_num;
      cache[index].disk_num = disk_num;
      cache[index].valid = true;
      clock += 1;
      cache[index].access_time = clock;
      return 1;
    }
    //Least Recently Used Algorithim to evict least recently used entry and replace it with new one.
    if(cache[index].access_time < cache[lru_index].access_time) {
      lru_index = index;
    }
    index += 1;
  }
  //Copy contents of buffer into the cache and update the cache entry properties
  memcpy(cache[lru_index].block, buf, 256);
  cache[lru_index].block_num = block_num;
  cache[lru_index].disk_num = disk_num;
  cache[lru_index].valid = true;
  clock += 1;
  cache[lru_index].access_time = clock;
  return 1;
}

bool cache_enabled(void) {
  //Cache parameters checked in previous code
  return (cache != NULL);
}

void cache_print_hit_rate(void) {
  fprintf(stderr, "Hit rate: %5.1f%%\n", 100 * (float) num_hits / num_queries);
}