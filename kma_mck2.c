/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the McKusick-Karels
 *              algorithm
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.2 $
 *    Last Modification: $Date: 2009/10/31 21:28:52 $
 *    File: $RCSfile: kma_mck2.c,v $
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: kma_mck2.c,v $
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
#ifdef KMA_MCK2
#define __KMA_IMPL__

/************System include***********************************************/
#include <assert.h>
#include <stdlib.h>
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

#define FALSE 0
#define TRUE 1
#define MAXSPACE (PAGESIZE - sizeof(kpage_t*) - sizeof(mck2Header_t))
#define PAGEOFFSET 0x1fff
#define MASKOFFSET ~PAGEOFFSET

#define NDX(size) \
					(size < BUFSIZE0) ? 0 \
				: (size < BUFSIZE1) ? 1 \
				: (size < BUFSIZE2) ? 2 \
				: (size < BUFSIZE3) ? 3 \
				: (size < BUFSIZE4) ? 4 \
				: (size < BUFSIZE5) ? 5 \
				: (size < BUFSIZE6) ? 6 \
				: (size < BUFSIZE7) ? 7 \
				: 8;

#define MAXSET 9
#define BUFSIZE0 1<<5
#define BUFSIZE1 1<<6
#define BUFSIZE2 1<<7
#define BUFSIZE3 1<<8
#define BUFSIZE4 1<<9
#define BUFSIZE5 1<<10
#define BUFSIZE6 1<<11
#define BUFSIZE7 1<<12
#define BUFSIZE8 MAXSPACE 
#define SPACE(idx) \
					(idx == 0) ? BUFSIZE0 \
				:	(idx == 1) ? BUFSIZE1 \
				:	(idx == 2) ? BUFSIZE2 \
				:	(idx == 3) ? BUFSIZE3 \
				:	(idx == 4) ? BUFSIZE4 \
				:	(idx == 5) ? BUFSIZE5 \
				:	(idx == 6) ? BUFSIZE6 \
				:	(idx == 7) ? BUFSIZE7 \
				:	(idx == 8) ? BUFSIZE8 \
				: BUFSIZE8

// Header in each buffer
typedef struct buf_header
{
	void* ptr;
} bufHeader_t;

// Header in page
typedef struct mck2_header
{
	kma_size_t size;
	int used;
	struct mck2_header* prePagePtr;
	struct mck2_header* nextPagePtr;
	bufHeader_t* bufferPtr;
} mck2Header_t;

/************Global Variables*********************************************/
// Pointer to the current page header
mck2Header_t* mck2Ptr = NULL;

/************Function Prototypes******************************************/
// search the free buffer in pages
mck2Header_t* searchFreelist(kma_size_t);

// initialize the page header
int initMck2(kma_size_t);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

void*
kma_malloc(kma_size_t size)
{
	// check if the request is valid
	if(size > PAGESIZE)
	{
		printf("ERROR: not enough space!\n");
		return NULL;
	}

	// if no page is present in kernel
	// initialize a new page
	if(mck2Ptr == NULL)
	{
		if(initMck2(size))
		{
			printf("ERROR: too large size!\n");
			return NULL;
		}
	}

	// if the request size larger than half page
	// return the whole page
	if(size > MAXSPACE / 2)
	{
		kpage_t* page;
		page = get_page();
		*((kpage_t**)(page->ptr)) = page;
		return page->ptr + sizeof(kpage_t*);
	}

	int index = NDX(size);
	kma_size_t reqSpace = SPACE(index);
	bufHeader_t* bufPtr = NULL;

//	printf("size: %d\tindex: %d\t request space: %d\n", size, index, reqSpace);
	bool reqNewPage;
	mck2Header_t* tempMck2Ptr;

	do{
		reqNewPage = FALSE;
		tempMck2Ptr = searchFreelist(reqSpace);	// find the next free buffer
		if(tempMck2Ptr == NULL)	// if no buffer available, initialize a new page
		{
			initMck2(size);
			reqNewPage = TRUE;
		}
		else	// return a buffer from the page
		{
			bufPtr = tempMck2Ptr->bufferPtr;
			tempMck2Ptr->bufferPtr = bufPtr->ptr;
			tempMck2Ptr->used += reqSpace;
			return (void*)bufPtr;
		}
	} while(reqNewPage);
  return NULL;
}

void
kma_free(void* ptr, kma_size_t size)
{
	// if the return size larger than half page
	// free the whole page
	if(size > MAXSPACE / 2)
	{
		kpage_t* page = *((kpage_t**)((void*)ptr - sizeof(kpage_t*)));
		free_page(page);
		return;
	}

	// find the page where the buffer is from
	mck2Header_t* tempMck2Ptr = (mck2Header_t*)(((unsigned long)ptr & MASKOFFSET) + sizeof(kpage_t*));

	if(tempMck2Ptr == NULL)
	{
		printf("ERROR: cannot find the return address!\n");
	}
	else	// return the buffer to the buffer list
	{
		int index = NDX(size);
		kma_size_t reqSpace = SPACE(index);
		bufHeader_t* bufPtr = (bufHeader_t*)ptr;
		bufPtr->ptr = tempMck2Ptr->bufferPtr;
		tempMck2Ptr->bufferPtr = bufPtr;
		tempMck2Ptr->used -= reqSpace;

		// if all buffers in that page are freed, return that page
		if(tempMck2Ptr->used == 0)
		{
			mck2Header_t* tempPtr;
			kpage_t* tempPagePtr;
			if((tempMck2Ptr->prePagePtr == NULL) && (tempMck2Ptr->nextPagePtr != NULL)) // if the page is the first page in the list
			{
				tempPtr = tempMck2Ptr->nextPagePtr;
				tempPtr->prePagePtr = NULL;
			}
			else if((tempMck2Ptr->prePagePtr != NULL) && (tempMck2Ptr->nextPagePtr == NULL)) // if the page is the last page in the list
			{
				tempPtr = tempMck2Ptr->prePagePtr;
				tempPtr->nextPagePtr = NULL;
				mck2Ptr = tempPtr;
			}
			else if((tempMck2Ptr->prePagePtr != NULL) && (tempMck2Ptr->nextPagePtr != NULL)) // if the page is in the middle of the list
			{
				tempPtr = tempMck2Ptr->prePagePtr;
				tempPtr->nextPagePtr = tempMck2Ptr->nextPagePtr;
				tempPtr = tempMck2Ptr->nextPagePtr;
				tempPtr->prePagePtr = tempMck2Ptr->prePagePtr;
			}
			else if((tempMck2Ptr->prePagePtr == NULL) && (tempMck2Ptr->nextPagePtr == NULL)) // if it is the only page in the list
			{
				mck2Ptr = NULL;
			}
			tempPagePtr = *((kpage_t**)((void*)tempMck2Ptr - sizeof(kpage_t*)));
			free_page(tempPagePtr);
		}
	}
}

// find a free buffer in the list
mck2Header_t* searchFreelist(kma_size_t reqSpace)
{
	mck2Header_t* curMck2Ptr = mck2Ptr;
	while(curMck2Ptr != NULL)
	{
		if((curMck2Ptr->size == reqSpace) && (curMck2Ptr->bufferPtr != NULL))
		{
			return curMck2Ptr;
		}
		curMck2Ptr = curMck2Ptr->prePagePtr;
	}	
	return NULL;
}

// initialize a new page
int initMck2(kma_size_t size)
{
	int index = NDX(size);
	kma_size_t reqSpace = SPACE(index);

	kpage_t* page;
	page = get_page();
	*((kpage_t**)page->ptr) = page;

	if((size + sizeof(kpage_t*) + sizeof(mck2Header_t)) > page->size)
	{
		free_page(page);
		return -1;
	}

	mck2Header_t* curMck2Ptr = (mck2Header_t*)((void*)page->ptr + sizeof(kpage_t*));
	mck2Header_t* preMck2Ptr = mck2Ptr;
	curMck2Ptr->size = reqSpace;
	curMck2Ptr->used = 0;
	curMck2Ptr->bufferPtr = NULL;

	if(mck2Ptr == NULL) // if it is the first page
	{
		curMck2Ptr->prePagePtr = NULL;
		curMck2Ptr->nextPagePtr = NULL;
	}
	else
	{
		curMck2Ptr->prePagePtr = preMck2Ptr;
		curMck2Ptr->nextPagePtr = NULL;
		preMck2Ptr->nextPagePtr = curMck2Ptr;
	}

	// cut the whole page into the same size buffer
	bufHeader_t* tempBufPtr = (bufHeader_t*)((void*)curMck2Ptr	+ sizeof(mck2Header_t));
	kma_size_t totalSpace = 0;
	while (totalSpace+reqSpace <= MAXSPACE)
	{
		tempBufPtr->ptr = curMck2Ptr->bufferPtr;
		curMck2Ptr->bufferPtr = tempBufPtr;
		tempBufPtr = (bufHeader_t*)((void*)tempBufPtr + reqSpace);
		totalSpace += reqSpace;
	}
	mck2Ptr = curMck2Ptr;
	return 0;
}
#endif // KMA_MCK2
