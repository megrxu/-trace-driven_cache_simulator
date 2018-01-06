/*
 * cache.c
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include "cache.h"
#include "main.h"

/* cache configuration parameters */
static int cache_split = 0;
static int cache_usize = DEFAULT_CACHE_SIZE;
static int cache_isize = DEFAULT_CACHE_SIZE;
static int cache_dsize = DEFAULT_CACHE_SIZE;
static int cache_block_size = DEFAULT_CACHE_BLOCK_SIZE;
static int words_per_block = DEFAULT_CACHE_BLOCK_SIZE / WORD_SIZE;
static int cache_assoc = DEFAULT_CACHE_ASSOC;
static int cache_writeback = DEFAULT_CACHE_WRITEBACK;
static int cache_writealloc = DEFAULT_CACHE_WRITEALLOC;

/* cache model data structures */
static Pcache icache;
static Pcache dcache;
static Pcache ucache;
static cache c1;
static cache c2;
static cache_stat cache_stat_inst;
static cache_stat cache_stat_data;

/************************************************************/
void set_cache_param(param, value) int param;
int value;
{
  switch (param)
  {
  case CACHE_PARAM_BLOCK_SIZE:
    cache_block_size = value;
    words_per_block = value / WORD_SIZE;
    break;
  case CACHE_PARAM_USIZE:
    cache_split = FALSE;
    cache_usize = value;
    break;
  case CACHE_PARAM_ISIZE:
    cache_split = TRUE;
    cache_isize = value;
    break;
  case CACHE_PARAM_DSIZE:
    cache_split = TRUE;
    cache_dsize = value;
    break;
  case CACHE_PARAM_ASSOC:
    cache_assoc = value;
    break;
  case CACHE_PARAM_WRITEBACK:
    cache_writeback = TRUE;
    break;
  case CACHE_PARAM_WRITETHROUGH:
    cache_writeback = FALSE;
    break;
  case CACHE_PARAM_WRITEALLOC:
    cache_writealloc = TRUE;
    break;
  case CACHE_PARAM_NOWRITEALLOC:
    cache_writealloc = FALSE;
    break;
  default:
    printf("error set_cache_param: bad parameter value\n");
    exit(-1);
  }
}
/************************************************************/

/************************************************************/
void init_cache()
{
  /* initialize the cache, and cache statistics data structures */
  //unified cache
  if (!cache_split)
  {
    ucache = &c1;
    ucache->size = cache_usize;
    ucache->associativity = cache_assoc;
    ucache->n_sets = cache_usize / cache_block_size / cache_assoc;
    ucache->index_mask_offset = LOG2(cache_block_size);
    ucache->index_mask = ~(0xffffffff << (ucache->index_mask_offset +
                                          LOG2(ucache->n_sets)));
    ucache->LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * ucache->n_sets);
    ucache->LRU_tail = ucache->LRU_head;
    ucache->set_contents = (int *)malloc(sizeof(int) * ucache->n_sets);
    for (int i = 0; i < ucache->n_sets; i++)
    {
      ucache->LRU_head[i] = NULL;
      ucache->set_contents[i] = 0;
    }
    ucache->contents = 0;
  }
  // splited cache
  else
  {
    icache = &c1;
    dcache = &c2;

    icache->size = cache_isize;
    icache->associativity = cache_assoc;
    icache->n_sets = cache_isize / cache_block_size / cache_assoc;
    icache->index_mask_offset = LOG2(cache_block_size);
    icache->index_mask = ~(0xffffffff << (icache->index_mask_offset +
                                          LOG2(icache->n_sets)));
    icache->LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * icache->n_sets);
    icache->LRU_tail = icache->LRU_head;
    icache->set_contents = (int *)malloc(sizeof(int) * icache->n_sets);
    for (int i = 0; i < icache->n_sets; i++)
    {
      icache->LRU_head[i] = NULL;
      icache->set_contents[i] = 0;
    }
    icache->contents = 0;

    dcache->size = cache_dsize;
    dcache->associativity = cache_assoc;
    dcache->n_sets = cache_dsize / cache_block_size / cache_assoc;
    dcache->index_mask_offset = LOG2(cache_block_size);
    dcache->index_mask = ~(0xffffffff << (dcache->index_mask_offset +
                                          LOG2(dcache->n_sets)));
    dcache->LRU_head = (Pcache_line *)malloc(sizeof(Pcache_line) * dcache->n_sets);
    dcache->LRU_tail = dcache->LRU_head;
    dcache->set_contents = (int *)malloc(sizeof(int) * dcache->n_sets);
    for (int i = 0; i < dcache->n_sets; i++)
    {
      dcache->LRU_head[i] = NULL;
      dcache->set_contents[i] = 0;
    }
    dcache->contents = 0;
  }
  //init cache stats
  cache_stat_inst.accesses = 0;
  cache_stat_inst.misses = 0;
  cache_stat_inst.replacements = 0;
  cache_stat_inst.demand_fetches = 0;
  cache_stat_inst.copies_back = 0;
  cache_stat_data.accesses = 0;
  cache_stat_data.misses = 0;
  cache_stat_data.replacements = 0;
  cache_stat_data.demand_fetches = 0;
  cache_stat_data.copies_back = 0;
}
/************************************************************/

/************************************************************/
void perform_access(addr, access_type) unsigned addr, access_type;
{
  /* handle an access to the cache */
  int index;
  int tag = 0;
  Pcache_line Pline;

  //compute the tag and index
  index = (addr & ucache->index_mask) >> ucache->index_mask_offset;
  tag = (addr & ~ucache->index_mask) >> (ucache->index_mask_offset +
                                         LOG2(ucache->n_sets));
  Pline = ucache->LRU_head[index];
  //determaine the access type
  switch (access_type)
  {
  //data access
  case TRACE_DATA_LOAD:
    cache_stat_data.accesses++;
    //miss
    if (Pline == NULL || Pline->tag != tag)
    {
      cache_stat_data.misses++;
      cache_stat_data.demand_fetches += (words_per_block);
      Pline = malloc(sizeof(cache_stat));
      Pline->dirty = FALSE;
      Pline->tag = tag;
      if (ucache->set_contents[index] == ucache->associativity)
      {
        if (ucache->LRU_tail[index]->dirty)
        {
          cache_stat_data.copies_back += words_per_block;
        }
        delete (&ucache->LRU_head[index], &ucache->LRU_tail[index], ucache->LRU_tail[index]);
        cache_stat_data.replacements++;
        ucache->set_contents[index]--;
      }
      insert(&ucache->LRU_head[index], &ucache->LRU_tail[index], Pline);
      ucache->set_contents[index]++;
    }
    break;
  case TRACE_DATA_STORE:
    cache_stat_data.accesses++;
    if (Pline == NULL || Pline->tag != tag)
    {
      cache_stat_data.misses++;
      if (cache_writealloc)
      {
        cache_stat_data.demand_fetches += (words_per_block);
      }
      Pline = malloc(sizeof(cache_stat));
      Pline->dirty = TRUE;
      Pline->tag = tag;
      if (ucache->set_contents[index] == ucache->associativity)
      {
        if (ucache->LRU_tail[index]->dirty)
        {
          cache_stat_data.copies_back += words_per_block;
        }
        delete (&ucache->LRU_head[index], &ucache->LRU_tail[index], ucache->LRU_tail[index]);
        cache_stat_data.replacements++;
        ucache->set_contents[index]--;
      }
      insert(&ucache->LRU_head[index], &ucache->LRU_tail[index], Pline);
      ucache->set_contents[index]++;
    }
    else
    {
      Pline->dirty = TRUE;
    }
    break;
  case TRACE_INST_LOAD:
    cache_stat_inst.accesses++;
    if (Pline == NULL || Pline->tag != tag)
    {
      cache_stat_inst.misses++;
      cache_stat_inst.demand_fetches += words_per_block;
      Pline = malloc(sizeof(cache_stat));
      Pline->dirty = FALSE;
      Pline->tag = tag;
      if (ucache->set_contents[index] == ucache->associativity)
      {
        if (ucache->LRU_tail[index]->dirty)
        {
          cache_stat_inst.copies_back += words_per_block;
        }
        delete (&ucache->LRU_head[index], &ucache->LRU_tail[index], ucache->LRU_tail[index]);
        cache_stat_inst.replacements++;
        ucache->set_contents[index]--;
      }
      insert(&ucache->LRU_head[index], &ucache->LRU_tail[index], Pline);
      ucache->set_contents[index]++;
    }
    break;
  }
}
/************************************************************/

/************************************************************/
void flush()
{
  /* flush the cache */
  Pcache_line Pline;
  for (int i = 0; i < ucache->n_sets; i++)
  {
    Pline = ucache->LRU_head[i];
    while (Pline != NULL)
    {
      if (Pline->dirty == TRUE)
      {
        cache_stat_data.copies_back += words_per_block;
        Pline->dirty = FALSE;
      }
      Pline = Pline->LRU_next;
    }
  }
}
/************************************************************/

/************************************************************/
void delete (head, tail, item)
    Pcache_line *head,
    *tail;
Pcache_line item;
{
  if (item->LRU_prev)
  {
    item->LRU_prev->LRU_next = item->LRU_next;
  }
  else
  {
    /* item at head */
    *head = item->LRU_next;
  }

  if (item->LRU_next)
  {
    item->LRU_next->LRU_prev = item->LRU_prev;
  }
  else
  {
    /* item at tail */
    *tail = item->LRU_prev;
  }
}
/************************************************************/

/************************************************************/
/* inserts at the head of the list */
void insert(head, tail, item)
    Pcache_line *head,
    *tail;
Pcache_line item;
{
  item->LRU_next = *head;
  item->LRU_prev = (Pcache_line)NULL;

  if (item->LRU_next)
    item->LRU_next->LRU_prev = item;
  else
    *tail = item;

  *head = item;
}
/************************************************************/

/************************************************************/
void dump_settings()
{
  printf("*** CACHE SETTINGS ***\n");
  if (cache_split)
  {
    printf("  Split I- D-cache\n");
    printf("  I-cache size: \t%d\n", cache_isize);
    printf("  D-cache size: \t%d\n", cache_dsize);
  }
  else
  {
    printf("  Unified I- D-cache\n");
    printf("  Size: \t%d\n", cache_usize);
  }
  printf("  Associativity: \t%d\n", cache_assoc);
  printf("  Block size: \t%d\n", cache_block_size);
  printf("  Write policy: \t%s\n",
         cache_writeback ? "WRITE BACK" : "WRITE THROUGH");
  printf("  Allocation policy: \t%s\n",
         cache_writealloc ? "WRITE ALLOCATE" : "WRITE NO ALLOCATE");
}
/************************************************************/

/************************************************************/
void print_stats()
{
  printf("\n*** CACHE STATISTICS ***\n");

  printf(" INSTRUCTIONS\n");
  printf("  accesses:  %d\n", cache_stat_inst.accesses);
  printf("  misses:    %d\n", cache_stat_inst.misses);
  if (!cache_stat_inst.accesses)
    printf("  miss rate: 0 (0)\n");
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n",
           (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses,
           1.0 - (float)cache_stat_inst.misses / (float)cache_stat_inst.accesses);
  printf("  replace:   %d\n", cache_stat_inst.replacements);

  printf(" DATA\n");
  printf("  accesses:  %d\n", cache_stat_data.accesses);
  printf("  misses:    %d\n", cache_stat_data.misses);
  if (!cache_stat_data.accesses)
    printf("  miss rate: 0 (0)\n");
  else
    printf("  miss rate: %2.4f (hit rate %2.4f)\n",
           (float)cache_stat_data.misses / (float)cache_stat_data.accesses,
           1.0 - (float)cache_stat_data.misses / (float)cache_stat_data.accesses);
  printf("  replace:   %d\n", cache_stat_data.replacements);

  printf(" TRAFFIC (in words)\n");
  printf("  demand fetch:  %d\n", cache_stat_inst.demand_fetches +
                                      cache_stat_data.demand_fetches);
  printf("  copies back:   %d\n", cache_stat_inst.copies_back +
                                      cache_stat_data.copies_back);
}
/************************************************************/
