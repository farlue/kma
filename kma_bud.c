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
#include <stdio.h>
#include <math.h>
#include <string.h>

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
#define MAXBUFCLASS (int)log2(PAGESIZE / MINBUFSIZE)
#define FIRSTPAGEHEADERSIZE (sizeof(pageHeader_t) + sizeof(buddyFreeLists_t))
#define PAGEHEADERSIZE (sizeof(pageHeader_t))

#define BITSPERCHAR 8
#define MAXBITMAPSEGS ((PAGESIZE / MINBUFSIZE) / BITSPERCHAR)
#define FREE 0
#define USED 1

struct bufferHeader;
typedef struct bufferHeader {
  struct bufferHeader* nextBuffer;
  struct bufferHeader* prevBuffer;
  void* pagePtr;
  kma_size_t size;
} bufferHeader_t;

typedef struct {
  kma_size_t size;
  bufferHeader_t* ptr;
} freeListHeader_t;

typedef struct {
  unsigned char bitSegments[MAXBITMAPSEGS];
} bitMap_t;

typedef struct {
  kpage_t* page;
  bitMap_t bitMap;
  int spaceUsed;
  int pageNo;
} pageHeader_t;

typedef struct {
  freeListHeader_t fl[MAXBUFCLASS];
  unsigned char hasFl;
  int pagesUsed;
  void* firstPagePtr;
} buddyFreeLists_t;

/************Global Variables*********************************************/
static buddyFreeLists_t* budfls;;

/************Function Prototypes******************************************/
bool
init(kma_size_t size);
void
header_alloc(void* pagePtr, kma_size_t headerSize);
void*
big_size_alloc(kma_size_t reqSize);
void*
buddy_alloc(kma_size_t reqSize);
freeListHeader_t*
coalesce(void* pagePtr, void* bufPtr, kma_size_t bufSize);
int
get_buf_class(kma_size_t bufSize);
void
get_buffer_from_large_buffer(void* pagePtr, kma_size_t reqBufSize,
			     kma_size_t largeBufSize, kma_size_t largeBufStartAddr);
void
add_buffer_to_free_list(void* pagePtr, int bufClass,
			kma_size_t startAddr, kma_size_t bufSize);
void
remove_buffer_from_free_list(bufferHeader_t* bufHdrPtr, int bufClass);
void
update_bitmap(void* pagePtr, void* bufPtr, kma_size_t bufSize, bool status);
int
lookup_bitmap(void* pagePtr, kma_size_t bufStartAddr, kma_size_t bufSize);
kma_size_t
get_roundup(kma_size_t reqSize);
	
/************External Declaration*****************************************/

/**************Implementation***********************************************/

void*
kma_malloc(kma_size_t size)
{
  /*printf("requested malloc size: %d\n", size);*/

  if (size > PAGESIZE) {
    error("requested size is larger than PAGESIZE.", "");
    return NULL;
  }

  if (budfls == NULL) {
    if (!init(size)) {
      return NULL;
    }
  }

  if (PAGESIZE / 2 < size) { // the requested size needs a new page
    return big_size_alloc(size);
  } else { // the requested size might fit in a free buffer
    return buddy_alloc(size);
  }

  return NULL;
}

void 
kma_free(void* ptr, kma_size_t size)
{
  /*  printf("requested free size: %d\n", size);
  if (size == 299) {
    printf("freeing #84: 266");
    }*/

  void* pagePtr = ptr - ((long)ptr % PAGESIZE);

  if (size > PAGESIZE / 2) {
    kpage_t* page;
    page = *((kpage_t**)(pagePtr));
    budfls->pagesUsed--;

    void* firstPagePtr = budfls->firstPagePtr;
    kma_size_t firstPageSpaceUsed = ((pageHeader_t*)(budfls->firstPagePtr))->spaceUsed;
    short pagesUsed = budfls->pagesUsed;

    //printf("freeing page %lx\n", (unsigned long)(pagePtr));
    free_page(page);

    if (pagePtr != firstPagePtr) {
      if (firstPageSpaceUsed == 0 &&
	  pagesUsed == 1) {
	printf("freeing first page %lx\n", (unsigned long)firstPagePtr);
	free_page(((pageHeader_t*)firstPagePtr)->page);
	budfls = NULL;
      }
    } else {
      budfls = NULL;
    }


    return;
  }

  kma_size_t bufSize = get_roundup(size);
  //int bufClass = get_buf_class(bufSize);
  //kma_size_t bufStartAddr = ptr - pagePtr;

  update_bitmap(pagePtr, ptr, bufSize, FREE);
  ((pageHeader_t*)pagePtr)->spaceUsed -= bufSize;

  if (((pageHeader_t*)pagePtr)->spaceUsed == 0) {
    kma_size_t sizeFreed;
    if (((pageHeader_t*)pagePtr)->pageNo == 0) {
      if (budfls->pagesUsed > 1) {
	coalesce(pagePtr, ptr, bufSize);
	return;
      } else {
	sizeFreed = get_roundup(FIRSTPAGEHEADERSIZE);
      }
    } else {
      sizeFreed = get_roundup(PAGEHEADERSIZE);
    }

    bufferHeader_t* bufPtr = pagePtr + sizeFreed;

    while (sizeFreed < PAGESIZE) {
      if ((void*)bufPtr != ptr) {
	sizeFreed += bufPtr->size;
	remove_buffer_from_free_list(bufPtr, get_buf_class(bufPtr->size));
	bufPtr = (void*)bufPtr + bufPtr->size;
      } else {
	sizeFreed += bufSize;
	bufPtr = (void*)bufPtr + bufSize;
      }
    }
    budfls->pagesUsed--;
    void* firstPagePtr = budfls->firstPagePtr;
    free_page(*((kpage_t**)pagePtr));

    if (pagePtr != firstPagePtr) {
      if (((pageHeader_t*)(budfls->firstPagePtr))->spaceUsed == 0 &&
	  budfls->pagesUsed == 1) {
	free_page(((pageHeader_t*)(budfls->firstPagePtr))->page);
	budfls = NULL;
      }
    } else {
      budfls = NULL;
    }

    return;
  }

  //add_buffer_to_free_list(pagePtr, get_buf_class(size), (int)(ptr - pagePtr), bufSize);

  coalesce(pagePtr, ptr, bufSize);

}

bool
init(kma_size_t size)
{
  kpage_t* page = get_page();
  *((kpage_t**)page->ptr) = page;

  ((pageHeader_t*)page->ptr)->pageNo = 0;

  if (page->size < size + PAGEHEADERSIZE) { // the requested size cannot fit in any page
    free_page(page);
    error("init: page size too small for requested size.", "");
    return FALSE;
  }

  budfls = page->ptr + PAGEHEADERSIZE;
  budfls->hasFl = 0x00;
  budfls->pagesUsed = 1;
  budfls->firstPagePtr = page->ptr;
  int i, bufSize = PAGESIZE / 2;
  for (i = 0; i < MAXBUFCLASS; i++) {
    (budfls->fl[i]).size = bufSize;
    (budfls->fl[i]).ptr = NULL;
    bufSize /= 2;
  }

  header_alloc(page->ptr, FIRSTPAGEHEADERSIZE);

  return TRUE;
}

void
header_alloc(void* pagePtr, kma_size_t headerSize)
{
  get_buffer_from_large_buffer(pagePtr, headerSize, PAGESIZE, 0);
  //update bitmap
  memset(pagePtr + sizeof(kpage_t*), '\000', MAXBITMAPSEGS);
  ((pageHeader_t*)pagePtr)->spaceUsed = 0;
}

void*
big_size_alloc(kma_size_t reqSize)
{
  kpage_t* page = get_page();
  *((kpage_t**)page->ptr) = page;
  budfls->pagesUsed++;

  return page->ptr + sizeof(kpage_t*) + sizeof(bitMap_t);
}

void*
buddy_alloc(kma_size_t reqSize)
{
  kma_size_t reqBufSize = get_roundup(reqSize);
  int reqBufClass = get_buf_class(reqBufSize);
  int bufClass = reqBufClass;
  void* bufPtr;
  while ((int)(budfls->hasFl & (1 << bufClass)) == 0 && bufClass >= 0) {
    bufClass--;
  }
  if (bufClass == reqBufClass) {
    bufPtr = (budfls->fl[bufClass]).ptr;
    remove_buffer_from_free_list((budfls->fl[bufClass]).ptr, bufClass);
    update_bitmap(((bufferHeader_t*)bufPtr)->pagePtr, bufPtr, reqBufSize, USED);
  } else {
    if (bufClass < 0) {
      kpage_t* page = get_page();
      *((kpage_t**)page->ptr) = page;
      ((pageHeader_t*)page->ptr)->pageNo = 1;
      budfls->pagesUsed++;
      header_alloc(page->ptr, PAGEHEADERSIZE);
      return buddy_alloc(reqSize);
    } else {
      bufPtr = (budfls->fl[bufClass]).ptr;
      remove_buffer_from_free_list((budfls->fl[bufClass]).ptr, bufClass);
      get_buffer_from_large_buffer(((bufferHeader_t*)bufPtr)->pagePtr, reqBufSize,
				   (budfls->fl[bufClass]).size,
				   (kma_size_t)(bufPtr - ((bufferHeader_t*)bufPtr)->pagePtr));
      update_bitmap(((bufferHeader_t*)bufPtr)->pagePtr, bufPtr, reqBufSize, USED);
    }
  }
  ((pageHeader_t*)(((bufferHeader_t*)bufPtr)->pagePtr))->spaceUsed += reqBufSize;  
  return bufPtr;
}

freeListHeader_t*
coalesce(void* pagePtr, void* bufPtr, kma_size_t bufSize)
{
  int bufClass = get_buf_class(bufSize);
  kma_size_t bufStartAddr = bufPtr - pagePtr;
  void* buddyPtr = bufStartAddr % (bufSize * 2) == bufSize ?
    bufPtr - bufSize : bufPtr + bufSize;
  kma_size_t buddyStartAddr = buddyPtr - pagePtr;
  int buddyUsed = lookup_bitmap(pagePtr, buddyStartAddr, bufSize);

  if (buddyPtr == pagePtr) {
    add_buffer_to_free_list(pagePtr, bufClass, bufStartAddr, bufSize);
    return &(budfls->fl[bufClass]);
  }
  if (buddyUsed == 0 && ((bufferHeader_t*)buddyPtr)->size == bufSize) {
    remove_buffer_from_free_list((bufferHeader_t*)buddyPtr, bufClass);
    void* bigBufPtr = bufPtr < buddyPtr ? bufPtr : buddyPtr;
    return coalesce(pagePtr, bigBufPtr, bufSize * 2);
  } else {
    add_buffer_to_free_list(pagePtr, bufClass, bufStartAddr, bufSize);
    return &(budfls->fl[bufClass]);
  }
}

int
get_buf_class(kma_size_t bufSize)
{
  return (int)log2(PAGESIZE / bufSize) - 1;
}

void
get_buffer_from_large_buffer(void* pagePtr, kma_size_t reqBufSize,
			     kma_size_t largeBufSize, kma_size_t largeBufStartAddr)
{
  kma_size_t bufSize = largeBufSize / 2;
  int bufClass = get_buf_class(bufSize); // get the class of the buffer
  kma_size_t freeBufStartAddr;
  while (bufSize >= MINBUFSIZE) {
    freeBufStartAddr = largeBufStartAddr + bufSize;
    add_buffer_to_free_list(pagePtr, bufClass, freeBufStartAddr, bufSize);
    if (bufSize / 2 < reqBufSize) { // found the right buffer size for header
      return;
    } else {
      bufSize /= 2;
      bufClass ++;
    }
  }
}

void
add_buffer_to_free_list(void* pagePtr, int bufClass,
			kma_size_t bufStartAddr, kma_size_t bufSize)
{
  bufferHeader_t* flhead = (budfls->fl[bufClass]).ptr; // put the buddy into a free list
  bufferHeader_t* bufHdrPtr = (bufferHeader_t*)(pagePtr + bufStartAddr);
  bufHdrPtr->nextBuffer = flhead;
  bufHdrPtr->prevBuffer = NULL;
  bufHdrPtr->pagePtr = pagePtr;
  bufHdrPtr->size = bufSize;
  if (flhead != NULL) {
    flhead->prevBuffer = bufHdrPtr;
  }
  (budfls->fl[bufClass]).ptr = bufHdrPtr;
  budfls->hasFl |= 1 << bufClass;
}

void
remove_buffer_from_free_list(bufferHeader_t* bufHdrPtr, int bufClass)
{
  // get a buffer from a free list
  bufferHeader_t* prevBufHdrPtr = bufHdrPtr->prevBuffer;
  bufferHeader_t* nextBufHdrPtr = bufHdrPtr->nextBuffer;
  if (prevBufHdrPtr != NULL) {
    prevBufHdrPtr->nextBuffer = bufHdrPtr->nextBuffer;
  }
  if (nextBufHdrPtr != NULL) {
    nextBufHdrPtr->prevBuffer = bufHdrPtr->prevBuffer;
  }
  if (bufHdrPtr == (budfls->fl[bufClass]).ptr) {
    (budfls->fl[bufClass]).ptr = nextBufHdrPtr;
    if (nextBufHdrPtr == NULL) {
      budfls->hasFl &= 0xff - (1 << bufClass);
    }
  }
}

void
update_bitmap(void* pagePtr, void* bufPtr, kma_size_t bufSize, bool status)
{
  int totalBits = bufSize / MINBUFSIZE;
  int i;
  unsigned char mask = 0xff;
  bitMap_t* bitMapLoc = pagePtr + sizeof(kpage_t*);
  kma_size_t bufStartAddr = (long)bufPtr - (long)pagePtr;
  int segNo = (bufStartAddr / MINBUFSIZE) / BITSPERCHAR;
  int bitNo = (bufStartAddr / MINBUFSIZE) % BITSPERCHAR;
  if (totalBits < BITSPERCHAR) {
    for (i = bitNo; i < bitNo + totalBits; i++) {
      mask -= 1 << i;
    }
    if (status == FREE) {
      bitMapLoc->bitSegments[segNo] &= mask;
    } else {
      bitMapLoc->bitSegments[segNo] |= ~mask;
    }
    return;
  }

  if (status == FREE) {
    memset((void*)bitMapLoc + segNo, '\000', totalBits / BITSPERCHAR);
  } else {
    memset((void*)bitMapLoc + segNo, '\377', totalBits / BITSPERCHAR);
  }

  /*  printf("page %lx bitMap after size %d: ", (unsigned long)pagePtr, bufSize);
  for (i = 0; i < MAXBITMAPSEGS; i++) {
    printf("%x ", (unsigned int)bitMapLoc->bitSegments[i]);
  }
  printf(" -- %ld; %d\n", sizeof(bitMap_t), i);
  printf(" ");*/
}

int
lookup_bitmap(void* pagePtr, kma_size_t bufStartAddr, kma_size_t bufSize)
{
  bitMap_t* bitMapLoc = pagePtr + sizeof(kpage_t*);
  int segNo = (bufStartAddr / MINBUFSIZE) / BITSPERCHAR;
  int bitNo = (bufStartAddr / MINBUFSIZE) % BITSPERCHAR;
  return (int)(bitMapLoc->bitSegments[segNo] & (1 << bitNo));
}

kma_size_t
get_roundup(kma_size_t reqSize)
{
  if (reqSize < MINBUFSIZE) {
    return MINBUFSIZE;
  }
  kma_size_t bufSize = reqSize;
  bufSize--;
  bufSize |= bufSize >> 1;
  bufSize |= bufSize >> 2;
  bufSize |= bufSize >> 4;
  bufSize |= bufSize >> 8;
  return bufSize + 1;
}
#endif // KMA_BUD
