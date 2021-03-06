/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the power-of-two free list
 *             algorithm
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.2 $
 *    Last Modification: $Date: 2009/10/31 21:28:52 $
 *    File: $RCSfile: kma_p2fl.c,v $
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: kma_p2fl.c,v $
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
#ifdef KMA_P2FL
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
#define TRUE	1
#define MAXSPACE (PAGESIZE - sizeof(kpage_t*) - sizeof(kflHeader_t) - sizeof(bufHeader_t))
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
#define NDX(size) \
					(size <= BUFSIZE0) ? 0 \
				: (size <= BUFSIZE1) ? 1 \
				: (size <= BUFSIZE2) ? 2 \
				: (size <= BUFSIZE3) ? 3 \
				: (size <= BUFSIZE4) ? 4 \
				: (size <= BUFSIZE5) ? 5 \
				: (size <= BUFSIZE6) ? 6 \
				: (size <= BUFSIZE7) ? 7 \
				: 8;

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
				: BUFSIZE8;

// Buffer header on the top of each buffer
typedef struct buffer_header
{
	void* ptr;
} bufHeader_t;

// Header in each page right after the page pointer
typedef struct k_freelist
{
	int spaceUsed;
	kma_size_t freespaceSize;
	void* freespacePtr;
	bufHeader_t* p2fl[MAXSET];
	struct k_freelist* prev;
} kflHeader_t;

/************Global Variables*********************************************/
// Pointer to the current page header
kflHeader_t* kflPtr = NULL;

/************Function Prototypes******************************************/
// Initialize the header in the new page
// Also, contains the freelist of all available buffers
int initKFL(kma_size_t);

// free all pages
void cleanupKFL();

// If the space left in the page cannot meet the request
// cut the space into smaller size and put them on freelist
void allocSpaceLeft(int);
/************External Declaration*****************************************/

/**************Implementation***********************************************/

void*
kma_malloc(kma_size_t size)
{
	// Check if the request is valid
	if(size > MAXSPACE)
	{
		printf("ERROR: too large request!\n");
		return NULL;
	}

	// If no page is present in kernel
	// get a new page and initialize the header
	if(kflPtr == NULL)
	{
		if(initKFL(size))
			return NULL;
	}

	// If the request size is larger than half page
	// simply return a whole page
	if(size > MAXSPACE / 2)
	{
		kpage_t* page;
		page = get_page();
		*((kpage_t**)(page->ptr)) = page;
		return page->ptr + sizeof(kpage_t*);
	}
	
	// Roundup the size and calculate the index and size
	int index = NDX(size + sizeof(bufHeader_t));
	kma_size_t reqSpace = SPACE(index);
	bufHeader_t* bufPtr;
	bool reqNewPage;
//	printf("size: %d\tindex: %d\t request space: %d\n", size, index, reqSpace);

	do {
		reqNewPage = FALSE;
		if(kflPtr->p2fl[index] == NULL)		// No avalible buffer in freelist
		{
			if((reqSpace <= kflPtr->freespaceSize)) // cut a buffer from the current page
			{
				bufPtr = (bufHeader_t*) kflPtr->freespacePtr;
				kflPtr->freespaceSize -= reqSpace;
				kflPtr->freespacePtr += reqSpace;
				kflPtr->spaceUsed += reqSpace;
				return (void*)bufPtr + sizeof(bufHeader_t);
			}
			else	// get a new page and initialize the header
			{
				allocSpaceLeft(index-1);
				initKFL(size);
				reqNewPage = TRUE;
			}
		}
		else	// remove the buffer from the freelist and return it
		{
			bufPtr = kflPtr->p2fl[index];
			kflPtr->p2fl[index] = (bufHeader_t*)bufPtr->ptr;
			kflPtr->spaceUsed += (int)reqSpace;
			return (void*)bufPtr + sizeof(bufHeader_t);
		}
	}while(reqNewPage);
	return NULL;
}

void
kma_free(void* ptr, kma_size_t size)
{
	// return size is larger than half page
	// simply free the page
	if(size > MAXSPACE / 2)
	{
		kpage_t* page = *((kpage_t**)((void*)ptr - sizeof(kpage_t*)));
		free_page(page);
		return;
	}

	// put the return buffer into the freelist
	int index = NDX(size + sizeof(bufHeader_t));
	kma_size_t reqSpace = SPACE(index);
 	bufHeader_t* bufPtr;
	bufPtr = (bufHeader_t*)(ptr - sizeof(bufHeader_t));
	bufPtr->ptr = kflPtr->p2fl[index];
	kflPtr->p2fl[index] = bufPtr;
	kflPtr->spaceUsed -= reqSpace;

	// if all buffers are returned, free all pages
	cleanupKFL();
}

// put the rest of the page into the freelist
void
allocSpaceLeft(int index)
{
	bufHeader_t* bufPtr;
	kma_size_t reqSpace;
	while(index >= 0)
	{
		reqSpace = SPACE(index);
		while((kflPtr->freespaceSize - reqSpace) >= 0)
		{	
			bufPtr = (bufHeader_t*) kflPtr->freespacePtr;
			bufPtr->ptr = (void*)&kflPtr->p2fl[index];
			kflPtr->freespaceSize -= reqSpace;
			kflPtr->freespacePtr += reqSpace;
		}
		index--;
	}
}

// free all pages in kernel
void
cleanupKFL()
{
	if(kflPtr->spaceUsed == 0)
	{
		kflHeader_t* tempKflPtr = kflPtr;
		kpage_t* tempPagePtr = NULL;
		while(tempKflPtr != NULL)
		{
			tempPagePtr = *((kpage_t**)((void*)tempKflPtr - sizeof(kpage_t*)));
			tempKflPtr = tempKflPtr->prev;
			free_page(tempPagePtr);
		}
		kflPtr = NULL;
	}
}

// initialize the new page
int initKFL(kma_size_t size)
{
	kpage_t* page;
	page = get_page();

	*((kpage_t**)page->ptr) = page;

	if((size + sizeof(kpage_t*) + sizeof(kflHeader_t)) > page->size)
	{
		free_page(page);
		return -1;
	}
	
	// initialize the header in the new page
	kflHeader_t* curKflPtr;
	kflHeader_t* preKflPtr;
	curKflPtr = page->ptr + sizeof(kpage_t*);

	curKflPtr->freespaceSize = page->size - sizeof(kpage_t*) - sizeof(kflHeader_t);
	curKflPtr->freespacePtr = page->ptr + sizeof(kpage_t*) + sizeof(kflHeader_t);

	if(kflPtr == NULL)
	{
		preKflPtr = NULL;
		curKflPtr->spaceUsed = 0;
		curKflPtr->prev = NULL;	
	}
	else
	{
		preKflPtr = kflPtr;
		curKflPtr->spaceUsed = preKflPtr->spaceUsed;
		curKflPtr->prev = preKflPtr;
	}

	// initialize the freelist
	// copy the free buffer pointers into the current page
	int i;
	for(i=0; i<MAXSET; ++i)
	{
		if(kflPtr == NULL)
			curKflPtr->p2fl[i] = NULL;
		else
			curKflPtr->p2fl[i] = preKflPtr->p2fl[i];
	}
	kflPtr = curKflPtr;	
//	printf("how many pages are spaceUsed?\t%d\n", curKflPtr->id+1);
	return 0;
}

#endif // KMA_P2FL
