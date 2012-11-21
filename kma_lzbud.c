/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the SVR4 lazy budy
 *             algorithm
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.2 $
 *    Last Modification: $Date: 2009/10/31 21:28:52 $
 *    File: $RCSfile: kma_lzbud.c,v $
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: kma_lzbud.c,v $
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
#ifdef KMA_LZBUD
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
#include <math.h>
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
/* buffer size, number of buffer types, and page header sizes */
#define MINBUFSIZE 32
#define MAXBUFCLASS (int)log2(PAGESIZE / MINBUFSIZE)
#define FIRSTPAGEHEADERSIZE (sizeof(pageHeader_t) + sizeof(buddyFreeLists_t))
#define PAGEHEADERSIZE (sizeof(pageHeader_t))

/* number of bits per char, and maximal number of bitmap segments */
#define BITSPERCHAR 8
#define MAXBITMAPSEGS ((PAGESIZE / MINBUFSIZE) / BITSPERCHAR)

/* bitmap annotation */
#define FREE 0
#define USED 1

/* buffer header */
struct bufferHeader;
typedef struct bufferHeader {
  struct bufferHeader* nextBuffer;
  struct bufferHeader* prevBuffer;
  void* pagePtr;
  unsigned char bufClass;
  unsigned char delayed;
} bufferHeader_t;

/* free list header */
typedef struct {
  kma_size_t size;
  bufferHeader_t* ptr;
  bufferHeader_t* tail;
} freeListHeader_t;

/* bitmap */
typedef struct {
  unsigned char bitSegments[MAXBITMAPSEGS];
} bitMap_t;

/* page header */
typedef struct {
  kpage_t* page;
  bitMap_t bitMap;
  kma_size_t spaceUsed;
} pageHeader_t;

/* counters for different buffer status */
typedef struct {
  short active;
  short locFree;
} bufferStatusList_t;

/* central structure for all free lists,
 * buffer status, and metadata
 */
typedef struct {
  freeListHeader_t fl[MAXBUFCLASS];
  bufferStatusList_t bs[MAXBUFCLASS];
  short pagesUsed;
  void* firstPagePtr;
} buddyFreeLists_t;

/************Global Variables*********************************************/
static buddyFreeLists_t* budfls = NULL;

/************Function Prototypes******************************************/
bool
init(kma_size_t size);
void
header_alloc(void* pagePtr, kma_size_t headerSize);
void*
big_size_alloc(kma_size_t reqSize);
void*
buddy_alloc(kma_size_t reqSize);
void
lazy_coalesce(void* pagePtr, void* bufPtr, int bufClass, kma_size_t bufSize);
freeListHeader_t*
coalesce(void* pagePtr, void* bufPtr, kma_size_t bufSize);
unsigned char
get_buf_class(kma_size_t bufSize);
kma_size_t
get_buf_size(unsigned char bufClass);
void
get_buffer_from_large_buffer(void* pagePtr, kma_size_t reqBufSize, unsigned char delayed,
			     kma_size_t largeBufSize, kma_size_t largeBufStartAddr);
bufferHeader_t*
find_buffer_in_free_list(void* pagePtr, void* bufPtr, unsigned char bufClass,
			 unsigned char delayed);
void
add_buffer_to_free_list_front(void* pagePtr, unsigned char bufClass, unsigned char delayed,
			kma_size_t startAddr, kma_size_t bufSize);
void
add_buffer_to_free_list_back(void* pagePtr, unsigned char bufClass, unsigned char delayed,
			kma_size_t startAddr, kma_size_t bufSize);
void
remove_buffer_from_free_list(bufferHeader_t* bufHdrPtr, unsigned char bufClass);
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

    /*printf("free: page %lx with space used %d\n", (unsigned long)pagePtr, ((pageHeader_t*)pagePtr)->spaceUsed);
      printf("freeing page %lx\n", (unsigned long)(pagePtr));*/
    free_page(page);

    if (pagePtr != firstPagePtr) {
      if (firstPageSpaceUsed == 0 &&
	  pagesUsed == 1) {
	/*printf("freeing first page %lx\n", (unsigned long)firstPagePtr);*/
	free_page(((pageHeader_t*)firstPagePtr)->page);
	budfls = NULL;
      }
    } else {
      budfls = NULL;
    }
    return;
  }

  kma_size_t bufSize = get_roundup(size);
  int bufClass = (int)get_buf_class(bufSize);
  //kma_size_t bufStartAddr = ptr - pagePtr;

  ((pageHeader_t*)pagePtr)->spaceUsed -= bufSize;
  /*if (size == 2491) {
    printf("\n");
    }

    printf("free: page %lx with space used %d\n", (unsigned long)pagePtr, ((pageHeader_t*)pagePtr)->spaceUsed);*/

  if (((pageHeader_t*)pagePtr)->spaceUsed == 0) {
    kma_size_t sizeFreed;
    if (pagePtr == budfls->firstPagePtr) {
      if (budfls->pagesUsed > 1) {
	lazy_coalesce(pagePtr, ptr, bufClass, bufSize);
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

	kma_size_t size = get_buf_size(bufPtr->bufClass);
	sizeFreed += size;
	remove_buffer_from_free_list(bufPtr, bufPtr->bufClass);
	bufPtr = (void*)bufPtr + get_buf_size(bufPtr->bufClass);
      } else {
	sizeFreed += bufSize;
	bufPtr = (void*)bufPtr + bufSize;
      }
    }
    assert(bufPtr == pagePtr + PAGESIZE);
    budfls->pagesUsed--;
    void* firstPagePtr = budfls->firstPagePtr;
    /*printf("freeing page %lx\n", (unsigned long)(pagePtr));*/
    free_page(*((kpage_t**)pagePtr));

    if (pagePtr != firstPagePtr) {
      if (((pageHeader_t*)(budfls->firstPagePtr))->spaceUsed == 0 &&
	  budfls->pagesUsed == 1) {
	/*printf("freeing page %lx\n", (unsigned long)(budfls->firstPagePtr));*/
	free_page(((pageHeader_t*)(budfls->firstPagePtr))->page);
	budfls = NULL;
      }
    } else {
      budfls = NULL;
    }

    return;
  }

  //add_buffer_to_free_list(pagePtr, get_buf_class(size), (int)(ptr - pagePtr), bufSize);
  lazy_coalesce(pagePtr, ptr, bufClass, bufSize);
}

bool
init(kma_size_t size)
{
  if (PAGESIZE < size + PAGEHEADERSIZE) { // the requested size cannot fit in any page
    error("init: page size too small for requested size.", "");
    return FALSE;
  }

  kpage_t* page = get_page();
  *((kpage_t**)page->ptr) = page;

  //((pageHeader_t*)page->ptr)->pageNo = 0;

  budfls = page->ptr + PAGEHEADERSIZE;
  budfls->pagesUsed = 1;
  budfls->firstPagePtr = page->ptr;
  int i, bufSize = PAGESIZE / 2;
  for (i = 0; i < MAXBUFCLASS; i++) {
    (budfls->fl[i]).size = bufSize;
    (budfls->fl[i]).ptr = NULL;
    (budfls->fl[i]).tail = NULL;
    (budfls->bs[i]).active = 0; 
    (budfls->bs[i]).locFree = 0; 
    bufSize /= 2;
  }

  header_alloc(page->ptr, FIRSTPAGEHEADERSIZE);

  return TRUE;
}

void
header_alloc(void* pagePtr, kma_size_t headerSize)
{
  get_buffer_from_large_buffer(pagePtr, headerSize, 0, PAGESIZE, 0);
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
  int reqBufClass = (int)get_buf_class(reqBufSize);
  int bufClass = reqBufClass;
  void* bufPtr;
  while ((budfls->fl[bufClass]).ptr == NULL && bufClass >= 0) {
    bufClass--;
  }
  if (bufClass == reqBufClass) {
    bufPtr = (budfls->fl[bufClass]).ptr;
    remove_buffer_from_free_list((budfls->fl[bufClass]).ptr, bufClass);
    if (((bufferHeader_t*)bufPtr)->delayed == 0) {
	update_bitmap(((bufferHeader_t*)bufPtr)->pagePtr, bufPtr, reqBufSize, USED);
    }
  } else {
    if (bufClass < 0) {
      kpage_t* page = get_page();
      *((kpage_t**)page->ptr) = page;
      //((pageHeader_t*)page->ptr)->pageNo = 1;
      budfls->pagesUsed++;
      header_alloc(page->ptr, PAGEHEADERSIZE);
      return buddy_alloc(reqSize);
    } else {
      bufPtr = (budfls->fl[bufClass]).ptr;
      remove_buffer_from_free_list((budfls->fl[bufClass]).ptr, bufClass);
      unsigned char delayed = ((bufferHeader_t*)bufPtr)->delayed;
      get_buffer_from_large_buffer(((bufferHeader_t*)bufPtr)->pagePtr, reqBufSize, delayed,
				   (budfls->fl[bufClass]).size,
				   (kma_size_t)(bufPtr - ((bufferHeader_t*)bufPtr)->pagePtr));
      if (delayed == 0) {
	update_bitmap(((bufferHeader_t*)bufPtr)->pagePtr, bufPtr, reqBufSize, USED);
      }
    }
  }

  ((pageHeader_t*)(((bufferHeader_t*)bufPtr)->pagePtr))->spaceUsed += reqBufSize;  

  (budfls->bs[reqBufClass]).active ++;
  ((bufferHeader_t*)bufPtr)->bufClass = -1;  
  return bufPtr;
}

void
lazy_coalesce(void* pagePtr, void* bufPtr, int bufClass, kma_size_t bufSize)
{
  short slack = (budfls->bs[bufClass]).active - (budfls->bs[bufClass]).locFree;
  if (slack > 1) {
    add_buffer_to_free_list_front(pagePtr, (unsigned char)bufClass, 1, (int)(bufPtr - pagePtr), bufSize);        
    (budfls->bs[bufClass]).locFree ++;
    return;
  } else {
    coalesce(pagePtr, bufPtr, bufSize);
  }
}

freeListHeader_t*
coalesce(void* pagePtr, void* bufPtr, kma_size_t bufSize)
{
  unsigned char bufClass = get_buf_class(bufSize);
  kma_size_t bufStartAddr = bufPtr - pagePtr;
  void* buddyPtr = bufStartAddr % (bufSize * 2) == bufSize ?
    bufPtr - bufSize : bufPtr + bufSize;
  kma_size_t buddyStartAddr = buddyPtr - pagePtr;
  int buddyUsed = lookup_bitmap(pagePtr, buddyStartAddr, bufSize);
  short slack = (budfls->bs[(int)bufClass]).active - (budfls->bs[(int)bufClass]).locFree;
  if (slack > 1) {
    add_buffer_to_free_list_front(pagePtr, bufClass, 1, bufStartAddr, bufSize);
    (budfls->bs[(int)bufClass]).locFree ++;
    return &(budfls->fl[(int)bufClass]);
  } 
  if (slack == 1) {
    update_bitmap(pagePtr, bufPtr, bufSize, FREE);
    if (buddyPtr == pagePtr) {
      add_buffer_to_free_list_back(pagePtr, bufClass, 0, bufStartAddr, bufSize);
      return &(budfls->fl[(int)bufClass]);
    }
    if (buddyUsed == 0 && ((bufferHeader_t*)buddyPtr)->bufClass == bufClass) {
      remove_buffer_from_free_list((bufferHeader_t*)buddyPtr, bufClass);
      void* bigBufPtr = bufPtr < buddyPtr ? bufPtr : buddyPtr;
      return coalesce(pagePtr, bigBufPtr, bufSize * 2);
    } else {
      add_buffer_to_free_list_back(pagePtr, bufClass, 0, bufStartAddr, bufSize);
      return &(budfls->fl[(int)bufClass]);
    }
  }
  if (buddyPtr == pagePtr) {
    add_buffer_to_free_list_front(pagePtr, bufClass, 1, bufStartAddr, bufSize);
    (budfls->bs[(int)bufClass]).locFree ++;
    return &(budfls->fl[(int)bufClass]);
  }
  bufferHeader_t* buddyHdrPtr = find_buffer_in_free_list(pagePtr, buddyPtr,
							 bufClass, 1);
  if (buddyHdrPtr != NULL) {
    remove_buffer_from_free_list((bufferHeader_t*)buddyPtr, bufClass);
    (budfls->bs[(int)bufClass]).locFree --;
    void* bigBufPtr = bufPtr < buddyPtr ? bufPtr : buddyPtr;
    return coalesce(pagePtr, bigBufPtr, bufSize * 2);
  } else {
    add_buffer_to_free_list_front(pagePtr, bufClass, 1, bufStartAddr, bufSize);
    (budfls->bs[(int)bufClass]).locFree ++;
    return &(budfls->fl[(int)bufClass]);
  }
}

unsigned char
get_buf_class(kma_size_t bufSize)
{
  return (unsigned char)((int)log2(PAGESIZE / bufSize) - 1);
}

kma_size_t
get_buf_size(unsigned char bufClass)
{
  return pow(2, MAXBUFCLASS - 1 - (int)bufClass) * MINBUFSIZE;
}

void
get_buffer_from_large_buffer(void* pagePtr, kma_size_t reqBufSize, unsigned char delayed,
			     kma_size_t largeBufSize, kma_size_t largeBufStartAddr)
{
  kma_size_t bufSize = largeBufSize / 2;
  int bufClass = (int)get_buf_class(bufSize); // get the class of the buffer
  kma_size_t freeBufStartAddr;
  while (bufSize >= MINBUFSIZE) {
    freeBufStartAddr = largeBufStartAddr + bufSize;
    if (delayed) {
      add_buffer_to_free_list_front(pagePtr, (unsigned char)bufClass, delayed, freeBufStartAddr, bufSize);
      (budfls->bs[bufClass]).locFree ++;
    } else {
      add_buffer_to_free_list_back(pagePtr, (unsigned char)bufClass, delayed, freeBufStartAddr, bufSize);
    }
    if (bufSize / 2 < reqBufSize) { // found the right buffer size for header
      return;
    } else {
      bufSize /= 2;
      bufClass ++;
    }
  }
}

bufferHeader_t*
find_buffer_in_free_list(void* pagePtr, void* bufPtr, unsigned char bufClass,
			 unsigned char delayed)
{
  bufferHeader_t* bufHdrPtr = (budfls->fl[(int)bufClass]).ptr; // find a buffer in a free list
  while (bufHdrPtr != NULL) {
    if ((void*)bufHdrPtr == bufPtr && bufHdrPtr->delayed == delayed) {
      return bufHdrPtr;
    }
    if (bufHdrPtr->delayed != delayed) {
      return NULL;
    }
    bufHdrPtr = bufHdrPtr->nextBuffer;
  }
  return NULL;
}

void
add_buffer_to_free_list_front(void* pagePtr, unsigned char bufClass, unsigned char delayed,
			     kma_size_t bufStartAddr, kma_size_t bufSize)
{
  bufferHeader_t* flhead = (budfls->fl[(int)bufClass]).ptr; // put the buffer into a free list
  bufferHeader_t* bufHdrPtr = (bufferHeader_t*)(pagePtr + bufStartAddr);
  bufHdrPtr->nextBuffer = flhead;
  bufHdrPtr->prevBuffer = NULL;
  bufHdrPtr->pagePtr = pagePtr;
  bufHdrPtr->bufClass = bufClass;
  bufHdrPtr->delayed = delayed;
  if (flhead != NULL) {
    flhead->prevBuffer = bufHdrPtr;
  } else {
    (budfls->fl[(int)bufClass]).tail = bufHdrPtr;
  }
  (budfls->fl[(int)bufClass]).ptr = bufHdrPtr;
}

void
add_buffer_to_free_list_back(void* pagePtr, unsigned char bufClass, unsigned char delayed,
			     kma_size_t bufStartAddr, kma_size_t bufSize)
{
  bufferHeader_t* fltail = (budfls->fl[(int)bufClass]).tail; // put the buffer into a free list
  bufferHeader_t* bufHdrPtr = (bufferHeader_t*)(pagePtr + bufStartAddr);
  bufHdrPtr->prevBuffer = fltail;
  bufHdrPtr->nextBuffer = NULL;
  bufHdrPtr->pagePtr = pagePtr;
  bufHdrPtr->bufClass = bufClass;
  bufHdrPtr->delayed = delayed;
  if (fltail != NULL) {
    fltail->nextBuffer = bufHdrPtr;
  } else {
    (budfls->fl[(int)bufClass]).ptr = bufHdrPtr;
  }
  (budfls->fl[(int)bufClass]).tail = bufHdrPtr;
}

void
remove_buffer_from_free_list(bufferHeader_t* bufHdrPtr, unsigned char bufClass)
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
  if (bufHdrPtr == (budfls->fl[(int)bufClass]).ptr) {
    (budfls->fl[(int)bufClass]).ptr = nextBufHdrPtr;
  }
  if (bufHdrPtr == (budfls->fl[(int)bufClass]).tail) {
    (budfls->fl[(int)bufClass]).tail = prevBufHdrPtr;
  }
  if (nextBufHdrPtr == NULL) {
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

  /*printf("page %lx bitMap after size %d: ", (unsigned long)pagePtr, bufSize);
  for (i = 0; i < MAXBITMAPSEGS; i++) {
    printf("%x ", (unsigned int)bitMapLoc->bitSegments[i]);
  }
  printf(" -- %ld; %d\n", sizeof(bitMap_t), i);
  printf(" ");*/
}

int
lookup_bitmap(void* pagePtr, kma_size_t bufStartAddr, kma_size_t bufSize)
{
  /*int totalBits = bufSize / MINBUFSIZE;
  int i;
  unsigned char mask = 0xff;
  bitMap_t* bitMapLoc = pagePtr + sizeof(kpage_t*);
  void* bufPtr = pagePtr + bufStartAddr;
  int used = 0;*/
  bitMap_t* bitMapLoc = pagePtr + sizeof(kpage_t*);
  int segNo = (bufStartAddr / MINBUFSIZE) / BITSPERCHAR;
  int bitNo = (bufStartAddr / MINBUFSIZE) % BITSPERCHAR;
  return (int)(bitMapLoc->bitSegments[segNo] & (1 << bitNo));
  /*if (totalBits < BITSPERCHAR) {
    for (i = bitNo; i < bitNo + totalBits; i++) {
      mask -= 1 << i;
    }
    used = bitMapLoc->bitSegments[segNo] & ~mask;
    return used;
  }
  for (i = 0; i < totalBits / BITSPERCHAR; i ++) {
    used |= bitMapLoc->bitSegments[segNo + i];
  }
  return used;*/
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
#endif // KMA_LZBUD
