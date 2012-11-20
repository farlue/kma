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
/*
#define MAXSET 5
#define BUFSIZE0 1<<8
#define BUFSIZE1 1<<9
#define BUFSIZE2 1<<11
#define BUFSIZE3 1<<12
#define BUFSIZE4 MAXSPACE 
#define NDX(size) \
					(size > BUFSIZE2) \
					? (size > BUFSIZE3) ? 4 : 3 \
					: (size > BUFSIZE1) \
						? 2	\
						: (size > BUFSIZE0) ? 1 : 0;
	
#define SPACE(idx) \
					(idx > 2) \
					? (idx > 3) ? BUFSIZE4 : BUFSIZE3 \
					: (idx > 1) \
						? BUFSIZE2 \
						: (idx > 0) ? BUFSIZE1: BUFSIZE0;
*/
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
					(size < BUFSIZE0) ? 0 \
				: (size < BUFSIZE1) ? 1 \
				: (size < BUFSIZE2) ? 2 \
				: (size < BUFSIZE3) ? 3 \
				: (size < BUFSIZE4) ? 4 \
				: (size < BUFSIZE5) ? 5 \
				: (size < BUFSIZE6) ? 6 \
				: (size < BUFSIZE7) ? 7 \
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


typedef struct
{
	void* ptr;
} bufHeader_t;

typedef struct
{
	kma_size_t size;
	int spaceUsed;
	int pagesUsed;
	bufHeader_t* freelistArr[MAXSET];
} mck2Header_t;

/************Global Variables*********************************************/

mck2Header_t* mck2Ptr = NULL;

/************Function Prototypes******************************************/

int initMck2(kma_size_t);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

void*
kma_malloc(kma_size_t size)
{
	if(size > PAGESIZE)
	{
		printf("ERROR: not enough space!\n");
	}
	else
	{
		if(mck2Ptr == NULL)
		{
			if(initMck2(size))
			{
				printf("ERROR: too large size!\n");
				return NULL;
			}
		}
	}

	int index;
	kma_size_t reqSpace;
	index = NDX(size);
	reqSpace = SPACE(index);
	bufHeader_t* bufPtr = NULL;
	bool reqNewPage;
//	printf("size: %d\tindex: %d\t request space: %d\n", size, index, reqSpace);
	do{
		reqNewPage = FALSE;
		if(mck2Ptr->freelistArr[index] == NULL)
		{
			initMck2(size);
			reqNewPage = TRUE;
		}
		else
		{
			bufPtr = mck2Ptr->freelistArr[index];
			mck2Ptr->freelistArr[index] = bufPtr->ptr;
			mck2Ptr->spaceUsed += reqSpace;
			return (void*)bufPtr;
		}
	} while(reqNewPage);
  return NULL;
}

void
kma_free(void* ptr, kma_size_t size)
{
  int index;
	kma_size_t reqSpace;
  index = NDX(size);
	reqSpace = SPACE(index);

	bufHeader_t* bufPtr = (bufHeader_t*)ptr;
	bufPtr->ptr = mck2Ptr->freelistArr[index];
	mck2Ptr->freelistArr[index] = bufPtr;
	mck2Ptr->spaceUsed -= reqSpace;

	if(mck2Ptr->spaceUsed == 0)
	{
		int i = mck2Ptr->pagesUsed + 1;
		mck2Header_t* tempMck2Ptr = mck2Ptr;
		kpage_t* tempPagePtr = NULL;
		while(i > 0)
		{
			tempPagePtr = *((kpage_t**)((void*)tempMck2Ptr - sizeof(kpage_t*)));
			free_page(tempPagePtr);
			tempMck2Ptr = (mck2Header_t*)((void*)tempMck2Ptr - PAGESIZE);
			i--;
		}
		mck2Ptr = NULL;		
	}
}

int initMck2(kma_size_t size)
{
	int index;
	kma_size_t reqSpace;
	index = NDX(size);
	reqSpace = SPACE(index);

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
	
	if(mck2Ptr == NULL)
	{
		curMck2Ptr->spaceUsed = 0;
		curMck2Ptr->pagesUsed = 0;
	}
	else
	{
		curMck2Ptr->spaceUsed = preMck2Ptr->spaceUsed;
		curMck2Ptr->pagesUsed = preMck2Ptr->pagesUsed + 1;
	}
	int i;
	for(i=0; i<MAXSET; ++i)
	{
		if(mck2Ptr == NULL)
			curMck2Ptr->freelistArr[i] = NULL;
		else
			curMck2Ptr->freelistArr[i] = preMck2Ptr->freelistArr[i];
	}

	bufHeader_t* tempBufPtr = (bufHeader_t*)((void*)curMck2Ptr	+ sizeof(mck2Header_t));

	kma_size_t totalSpace = 0;
	while (index >= 0)
	{
		reqSpace = SPACE(index);
		while (totalSpace+reqSpace <= MAXSPACE)
		{
			tempBufPtr->ptr = curMck2Ptr->freelistArr[index];
			curMck2Ptr->freelistArr[index] = tempBufPtr;
			tempBufPtr = (bufHeader_t*)((void*)tempBufPtr + reqSpace);
			totalSpace += reqSpace;
		}
		index--;
	}
	mck2Ptr = curMck2Ptr;
	return 0;
}

#endif // KMA_MCK2
