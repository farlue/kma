/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the buddy algorithm
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.2 $
 *    Last Modification: $Date: 2009/10/31 21:28:52 $
 *    File: $RCSfile: kma_bud.c,v $
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: kma_bud.c,v $
 *    Revision 1.2  2009/10/31 21:28:52  jot836
 *    This is the current version of KMA project 3.
 *    It includes:
 *    - the most up-to-date handout (F'09)
 *    - updated skeleton including
 *        file-driven test harness,
 *        trace generator script,
 *        support for evaluating efficiency of algorithm (wasted memory),
 *        gnuplot support for plotting allocation and waste,
 *        set of traces for all students to use (including a makefile and README of the settings),
 *    - different version of the testsuite for use on the submission site, including:
 *        scoreboard Python scripts, which posts the top 5 scores on the course webpage
 *
 *    Revision 1.1  2005/10/24 16:07:09  sbirrer
 *    - skeleton
 *
 *    Revision 1.2  2004/11/05 15:45:56  sbirrer
 *    - added size as a parameter to kma_free
 *
 *    Revision 1.1  2004/11/03 23:04:03  sbirrer
 *    - initial version for the kernel memory allocator project
 *
 ***************************************************************************/
#ifdef KMA_BUD
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/************Private include**********************************************/
#include "kpage.h"
#include "kma.h"

/************Defines and Typedefs*****************************************/
/*  #defines and typedefs should have their names in all caps.
 *  Global variables begin with g. Global constants with k. Local
 *  variables should be in all lower case. When initializing
 *  structures and arrays, line everything up in neat columns.
 */

#define MINBUFSIZE 32
#define LOGMINBUFSIZE 5
#define BITMAPSIZE (PAGESIZE / MINBUFSIZE / 8)
#define BUFCOUNT 8

#define PAGEOFFSET(x) ((int)(((long)(x)) &(PAGESIZE-1)) >> LOGMINBUFSIZE)
#define INDEX2SIZE(i) (1 << ((i) + LOGMINBUFSIZE))
#define BUDDYALLOCINFO ((struct buddyAlloc *)(gRootPage->ptr))

struct bufHeader
{
  struct bufHeader * prev;
  struct bufHeader * next;
  kma_size_t size;
};

struct pageInfo
{
  unsigned char bitmap[BITMAPSIZE];
  int block_count;
  kpage_t *page;
};

struct freeList
{
  struct bufHeader * head;
  int active;
  int local_free;
  int global_free;
};

struct buddyAlloc
{
  struct freeList free_lists[BUFCOUNT];
  unsigned char or_mask[3][8];
  unsigned char and_mask[3][8];
  long total_size;
};

/************Global Variables*********************************************/
static kpage_t * gRootPage = NULL;


/************Function Prototypes******************************************/
/* intializing and freeing global data used by buddy allocator */
void init_buddy_alloc();
void exit_buddy_alloc();

/* get_page() and split page into free buffers */
void init_new_page();

/* create a buffer */
void init_buffer(void * ptr, struct bufHeader * prev, struct bufHeader * next);

/* translate size to index */
kma_size_t size2index(kma_size_t size);

/* push and pop buffer from/to the the free list*/
void* pop_buf(kma_size_t index);
void push_buf(kma_size_t index, void * ptr);


void coalesce_buf(kma_size_t index, void * ptr);
/* show status (free/in use) of a block */
inline int block_status(void *ptr);

/* update bitmap */
void update_pageinfo(kma_size_t index, void * ptr, int op);
inline void * find_buddy(kma_size_t index, void * ptr);


/* helper function for debug use*/
void print_free_list(kma_size_t index);
void print_bitmap(void * ptr);
/************External Declaration*****************************************/

/**************Implementation***********************************************/
void init_buddy_alloc()
{
  gRootPage = get_page();
  struct buddyAlloc * buddyInfo = (struct buddyAlloc *) gRootPage->ptr;

  unsigned char or_mask[3][8] = {
    {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80},
    {0x03, 0x00, 0x0c, 0x00, 0x30, 0x00, 0xc0, 0x00},
    {0x0f, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00},
  };
  unsigned char and_mask[3][8] = {
    {0xfe, 0xfd, 0xfb, 0xf7, 0xef, 0xdf, 0xbf, 0x7f},
    {0xfc, 0xff, 0xf3, 0xff, 0xcf, 0xff, 0x3f, 0xff},
    {0xf0, 0xff, 0xff, 0xff, 0x0f, 0xff, 0xff, 0xff},
  };

  memset(buddyInfo->free_lists, 0, BUFCOUNT * sizeof(struct freeList));
  memcpy(buddyInfo->or_mask, or_mask, 24);
  memcpy(buddyInfo->and_mask, and_mask, 24);
  buddyInfo->total_size = 0;
}

void release_buffer(void * buf)
{
  struct bufHeader * bufh = (struct bufHeader *) buf;
  int index = size2index(bufh->size);
  struct bufHeader * prev = bufh->prev;
  struct bufHeader * next = bufh->next;
  if (prev == NULL)
    BUDDYALLOCINFO->free_lists[index].head = next;
  else
    prev->next = next;
  if (next != NULL)
    next->prev = prev;
  update_pageinfo(index, buf, 1);
}

void clean_page(void * ptr)
{
  void * buf = ptr + (MINBUFSIZE << 1);
  while(buf < ptr + PAGESIZE)
    {
      release_buffer(buf);
      buf += ((struct bufHeader *)buf)->size;
    }
  struct pageInfo * page_info = (struct pageInfo *) ptr;
  free_page(page_info->page);
}

void
update_pageinfo(kma_size_t index, void * ptr, int op)
{
  // update bitmap
  struct pageInfo * page_info = BASEADDR(ptr);
  int offset = PAGEOFFSET(ptr);

  if (op == 1)
    {
      if (index < 3)
        {
          page_info->bitmap[offset >> 3]
            |= BUDDYALLOCINFO->or_mask[index][offset & 7];
        }
      else
        memset(page_info->bitmap + (offset >> 3), 0xff, 1 << (index - 3));
      page_info->block_count += 1 << index;
    }
  else
    {
      if (index < 3)
        {
          page_info->bitmap[offset >> 3]
            &= BUDDYALLOCINFO->and_mask[index][offset & 7];
        }
      else
        memset(page_info->bitmap + (offset >> 3), 0, 1 << (index - 3));
      page_info->block_count -= 1 << index;
    }
}

void
print_free_list(kma_size_t index)
{
  struct bufHeader * ret = BUDDYALLOCINFO->free_lists[index].head;
  printf(">>> %d <<<\n", index);
  while(ret != NULL)
    {
      printf("a %lx\n", (unsigned long) ret);
      ret = ret->next;
    }
}

void
print_bitmap(void * ptr)
{
  struct pageInfo * page_info = BASEADDR(ptr);
  int i;
  for (i = 0; i < BITMAPSIZE; i ++)
    printf("%02x", page_info->bitmap[i]);
  printf("\n");
}

kma_size_t
size2index(kma_size_t size)
{
  kma_size_t index = 0;
  while (INDEX2SIZE(index) < size)
    ++index;
  return index;
}

void
init_buffer(void * ptr, struct bufHeader * prev, struct bufHeader * next)
{
  struct bufHeader * bptr = (struct bufHeader *) ptr;
  bptr->next = next;
  bptr->prev = prev;
}

void
push_buf(kma_size_t index, void * ptr)
{
  if (BUDDYALLOCINFO->free_lists[index].head == NULL)
    {
      init_buffer(ptr, NULL, NULL);
    }
  else
    {
      BUDDYALLOCINFO->free_lists[index].head->prev = ptr;
      init_buffer(ptr, NULL, BUDDYALLOCINFO->free_lists[index].head);
    }
  BUDDYALLOCINFO->free_lists[index].head = ptr;
  BUDDYALLOCINFO->free_lists[index].head->size = MINBUFSIZE << index;

  update_pageinfo(index, ptr, 0);
  assert(block_status(ptr) == 0);
}

void
init_new_page()
{
  kpage_t * page = get_page();
  void * ptr = page->ptr;
  kma_size_t index;
  kma_size_t size = MINBUFSIZE << 1;  /* page info takes 64 bytes*/

  struct pageInfo * page_info = (struct pageInfo*) ptr;
  memset(ptr, 0, sizeof(struct pageInfo));

  ptr += size;
  for (index = 1; index < BUFCOUNT; index++)
    {
      push_buf(index, ptr);
      ptr += size;
      size <<= 1;
    }
  page_info->block_count = 0;
  page_info->page = page;
}

void*
pop_buf(kma_size_t index)
{

  struct bufHeader * ptr = (void *)BUDDYALLOCINFO->free_lists[index].head;
  if (ptr->next != NULL)
    ptr->next->prev = NULL;
  BUDDYALLOCINFO->free_lists[index].head = ptr->next;

  update_pageinfo(index, ptr, 1);
  assert(block_status(ptr) != 0);
  return (void*) ptr;
}

void *
split_buf(kma_size_t index_from, kma_size_t index_to)
{
  void * ptr = pop_buf(index_from);
  while (index_from > index_to)
    {
      --index_from;
      push_buf(index_from, ptr + INDEX2SIZE(index_from));
    }
  return ptr;
}

int
block_status(void *ptr)
{
  struct pageInfo * page_info = BASEADDR(ptr);
  int offset = PAGEOFFSET(ptr);
  return page_info->bitmap[offset >> 3] & (1 << (offset & 7));
}

void *
find_buddy(kma_size_t index, void * ptr)
{
  void * xor = (void*)(long)(INDEX2SIZE(index));
  return (void*)((unsigned long)ptr ^ (unsigned long)xor);
}

void
coalesce_buf(kma_size_t index, void * ptr)
{
  void * buddy = find_buddy(index, ptr);
  while (buddy != BASEADDR(ptr) && block_status(buddy) == 0
         && ((struct bufHeader*)buddy)->size == ((struct bufHeader*)ptr)->size)
    {
      release_buffer(ptr);
      release_buffer(buddy);
      if (buddy < ptr)
        ptr = buddy;
      ++index;
      push_buf(index, ptr);

      if (index == BUFCOUNT)
        break;
      buddy = find_buddy(index, ptr);
    }
}

void*
kma_malloc(kma_size_t size)
{
  if (gRootPage == NULL)
    init_buddy_alloc();
  if (size > PAGESIZE - sizeof(void*))
    return NULL;
  if (size > PAGESIZE / 2)
    {
      kpage_t * page;
      page = get_page();
      *((kpage_t **)(page->ptr)) = page;
      return page->ptr + sizeof(kpage_t *);
    }
  BUDDYALLOCINFO->total_size += size;
  kma_size_t index = size2index(size);
  kma_size_t i;

  for (i = index; i < BUFCOUNT; i++) {
    if (BUDDYALLOCINFO->free_lists[i].head != NULL)
      return split_buf(i, index);
  }

  init_new_page();
  return pop_buf(index);
}

void
kma_free(void* ptr, kma_size_t size)
{
  if (size > PAGESIZE - sizeof(void*))
    return;
  if (size > PAGESIZE / 2)
    {
      kpage_t * page = *(kpage_t**)(BASEADDR(ptr));
      free_page(page);
      return;
    }

  kma_size_t index = size2index(size);
  push_buf(index, ptr);
  coalesce_buf(index, ptr);
  struct pageInfo * page_info = BASEADDR(ptr);
  if (page_info->block_count == 0)
    clean_page(page_info);

  BUDDYALLOCINFO->total_size -= size;
  if(BUDDYALLOCINFO->total_size == 0)
    {
      free_page(gRootPage);
      gRootPage = NULL;
    }
  return;
}

#endif // KMA_BUD
