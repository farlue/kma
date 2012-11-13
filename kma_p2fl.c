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
#define MAXSET 5 
#define MAXSPACE (PAGESIZE - sizeof(kpage_t*) - sizeof(kflHeader_t) - sizeof(bufHeader_t))

typedef struct buffer_header
{
	void* ptr;
} bufHeader_t;

typedef struct p2fl_header
{
	kma_size_t size;
	bufHeader_t* buffer;
} p2flHeader_t;

typedef struct k_freelist
{
	int pageUsed;
	int spaceUsed;
	kma_size_t freespaceSize;
	void* freespacePtr;
	p2flHeader_t p2fl[MAXSET];
} kflHeader_t;

/************Global Variables*********************************************/

kflHeader_t* kflPtr = NULL;

/************Function Prototypes******************************************/

int initKFL(kma_size_t);
int roundup(kma_size_t);
void cleanupKFL();
kma_size_t convertIdxToSize(int);

/************External Declaration*****************************************/

/**************Implementation***********************************************/

void*
kma_malloc(kma_size_t size)
{
	if(size > MAXSPACE)
	{
		printf("ERROR: not enough space!\n");
	}
	else
	{
		if(kflPtr == NULL)
		{
			if(initKFL(size))
			{
				printf("ERROR: too large size!\n");
				return NULL;
			}
		}
	}

	int index = roundup(size + sizeof(bufHeader_t));
	kma_size_t reqSpace = convertIdxToSize(index);

	bufHeader_t* bufPtr;
	bool reqNewPage;
//	printf("size: %d\tindex: %d\t request space: %d\n", size, index, reqSpace);

	do {
		reqNewPage = FALSE;
		if(kflPtr->p2fl[index].buffer == NULL)
		{
			if((reqSpace <= kflPtr->freespaceSize))
			{
				bufPtr = (bufHeader_t*) kflPtr->freespacePtr;
				bufPtr->ptr =(void*)&kflPtr->p2fl[index];
				kflPtr->freespaceSize -= reqSpace;
				kflPtr->freespacePtr += reqSpace;
				kflPtr->spaceUsed += (int)reqSpace;
				return (void*)bufPtr + sizeof(bufHeader_t);
			}
			else
			{
				initKFL(size);
				reqNewPage = TRUE;
			}
		}
		else
		{
			bufPtr = kflPtr->p2fl[index].buffer;
			kflPtr->p2fl[index].buffer = (bufHeader_t*)bufPtr->ptr;
			bufPtr->ptr = (void*)&kflPtr->p2fl[index];
			kflPtr->spaceUsed += (int)reqSpace;
			return (void*)bufPtr + sizeof(bufHeader_t);
		}
	}while(reqNewPage);
	return NULL;
}

void
kma_free(void* ptr, kma_size_t size)
{
 	bufHeader_t* bufPtr;
	p2flHeader_t* listPtr;
	int index = roundup(size + sizeof(bufHeader_t));

	bufPtr = (bufHeader_t*)(ptr - sizeof(bufHeader_t));
//	listPtr = (p2flHeader_t*) bufPtr->ptr;
	listPtr = &kflPtr->p2fl[index];
	bufPtr->ptr = (void*) listPtr->buffer;
	listPtr->buffer = bufPtr;
//	printf("free size: %d\tlist size: %d\n", size, listPtr->size);
	kflPtr->spaceUsed -= (int)listPtr->size;
	cleanupKFL();
}

void
cleanupKFL()
{
	if(kflPtr->spaceUsed == 0)
	{
		kflHeader_t* tempKflPtr = kflPtr;
		kpage_t* tempPagePtr = NULL;
		int i = tempKflPtr->pageUsed+1;
		while(i>0)
		{
			tempPagePtr = *((kpage_t**)((void*)tempKflPtr - sizeof(kpage_t*)));
			free_page(tempPagePtr);
			tempKflPtr = (kflHeader_t*)((void*)tempKflPtr - PAGESIZE);
			i--;
		}
		kflPtr = NULL;
	}
}

int initKFL(kma_size_t size)
{
	kpage_t* page;
	page = get_page();

	*((kpage_t**)page->ptr) = page;

	if((size + sizeof(kpage_t*) + sizeof(kflHeader_t) > page->size))
	{
		free_page(page);
		return -1;
	}
	
	kflHeader_t* curKflPtr;
	kflHeader_t* preKflPtr;
	curKflPtr = page->ptr + sizeof(kpage_t*);

	curKflPtr->freespaceSize = page->size - sizeof(kpage_t*) - sizeof(kflHeader_t);
	curKflPtr->freespacePtr = page->ptr + sizeof(kpage_t*) + sizeof(kflHeader_t);

	if(kflPtr == NULL)
	{
		preKflPtr = NULL;
		curKflPtr->pageUsed = 0;
		curKflPtr->spaceUsed = 0;	
	}
	else
	{
		preKflPtr = kflPtr;
		curKflPtr->pageUsed = preKflPtr->pageUsed + 1;
		curKflPtr->spaceUsed = preKflPtr->spaceUsed;
	}
	
	int i;
	for(i=0; i<MAXSET; ++i)
	{
		curKflPtr->p2fl[i].size = convertIdxToSize(i);
		if(kflPtr == NULL)
			curKflPtr->p2fl[i].buffer = NULL;
		else
			curKflPtr->p2fl[i].buffer = preKflPtr->p2fl[i].buffer;
	}
	kflPtr = curKflPtr;	
//	printf("how many pages are spaceUsed?\t%d\n", curKflPtr->id+1);
	return 0;
}

kma_size_t convertIdxToSize(int i)
{
/*	if(i == MAXSET-1)
		return (kma_size_t) MAXSPACE;
	else
		return (kma_size_t) 1<<(i+4);
*/
	switch (i)
	{
		case 0:
			return (kma_size_t) 1<<8;	
		case 1:
			return (kma_size_t) 1<<9;
		case 2:
			return (kma_size_t) 1<<11;
		case 3:
			return (kma_size_t) 1<<12;
		case MAXSET-1:
		default:
			return (kma_size_t) MAXSPACE;
	}
}

int roundup(kma_size_t size)
{
	int i;
	for(i=0; i<MAXSET; ++i)
	{
		if(size < convertIdxToSize(i))
			return i;
	}
	return MAXSET-1;
}
#endif // KMA_P2FL
