/***************************************************************************
 *  Title: Kernel Memory Allocator
 * -------------------------------------------------------------------------
 *    Purpose: Kernel memory allocator based on the resource map algorithm
 *    Author: Stefan Birrer
 *    Version: $Revision: 1.2 $
 *    Last Modification: $Date: 2009/10/31 21:28:52 $
 *    File: $RCSfile: kma_rm.c,v $
 *    Copyright: 2004 Northwestern University
 ***************************************************************************/
/***************************************************************************
 *  ChangeLog:
 * -------------------------------------------------------------------------
 *    $Log: kma_rm.c,v $
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
#ifdef KMA_RM
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

/************Global Variables*********************************************/
kpage_t *entry = NULL;

/************External Declaration*****************************************/
typedef struct
{
  int size;
  void *base;
  void *prev;
  void *next;
}bufhead;

typedef struct
{
  kpage_t **page;
  bufhead *freelist;
//  int counter;
}pagehead;
/************Function Prototypes******************************************/
void 
init();

void*
alloc(bufhead *buffer ,int size);

void 
giveback(bufhead *buffer);

void
merge(bufhead *buffer);

void
fpage();
/**************Implementation***********************************************/

void*
kma_malloc(kma_size_t size)
{
  kpage_t *newpage;
  pagehead *first, *second;
  bufhead  *buffer, *follow, *newbuf;

//  printf("%d\n",size); 
 
  if(size + sizeof(pagehead) + sizeof(bufhead) > PAGESIZE)
     return NULL;
   
  if(entry == NULL)
    init();
  
  first = (pagehead*)entry->ptr;
  buffer = (bufhead*)first->freelist;

  follow = NULL;
  while(buffer!=NULL && buffer->size < size)
  {
    follow = buffer;
    buffer = (bufhead*)buffer->next;
  }

  if(buffer != NULL)
    return alloc(buffer, size);
  else
  {
    newpage = get_page();
    *((kpage_t**)newpage->ptr) = newpage;
//    first->counter++;   
    second = (pagehead*)newpage->ptr;
    second->freelist = NULL;
//    second->counter = 0;
       
    newbuf = (bufhead*)((long int)newpage->ptr + sizeof(pagehead));
    newbuf->size = PAGESIZE - sizeof(pagehead) - sizeof(bufhead);
    newbuf->base = (void*)((long int)newbuf + sizeof(bufhead));
    
    if(follow == NULL)
    {
      first->freelist = newbuf;
      newbuf->next = NULL;
      newbuf->prev = NULL;
    }
    else
    {
      newbuf->prev = (void*)follow;
      newbuf->next = NULL;
      follow->next = (void*)newbuf;
    }
         
    return alloc(newbuf, size);
  }   
}


void
kma_free(void* ptr, kma_size_t size)
{
  pagehead *first;
  bufhead *buffer;
   
  first  = (pagehead*)entry->ptr;
  buffer = (bufhead*)((long int)ptr - sizeof(bufhead));
  
  giveback(buffer);

  merge(buffer);
 
  fpage(); 
}

void
fpage()
{
  kpage_t* page;
  pagehead*first, *second;
  bufhead *front, *rear;
  void* freepage;  

  first = (pagehead*)entry->ptr;

  front = (bufhead*)first->freelist;
  rear = NULL;
  
  if(front == NULL) return;
  
  while(front != NULL)
  {
    rear = front;
    front = (bufhead*)front->next;
  }//find the rear 
 
  front = (bufhead*)rear->prev;
  if(rear->size == PAGESIZE - sizeof(pagehead) - sizeof(bufhead))
  { 
    if(front == NULL)
       first->freelist = NULL;
    else
       front->next = NULL;
  
    freepage = (void*)((long int)rear - sizeof(pagehead));
    second = (pagehead*)freepage;
    page = (kpage_t*)second->page;
    
//    printf("page->id %d\n",page->id);
    if(page->id == 0)
    {
      entry = NULL;
      free_page(page);
      return;
    }
    else
    {
      if(first->freelist == NULL)
      {
        free_page(page);
        return;
      }
      else
      {
        free_page(page);
        fpage();
      } 
    }
  }
    
}


void init()
{
  pagehead *first;
  bufhead *buffer;
   
  entry = get_page();
  *((kpage_t**)entry->ptr) = entry;
     
  first = (pagehead*)entry->ptr;
  buffer = (bufhead*)((long int)entry->ptr + sizeof(pagehead)); 

  first->freelist = (void*)buffer;
//  first->counter = 1;

  buffer->size = PAGESIZE - sizeof(pagehead) - sizeof(bufhead);
  buffer->base = (void*)((long int)buffer + sizeof(bufhead));
  buffer->prev = NULL;
  buffer->next = NULL;    
}

void* alloc(bufhead* buffer, int size)
{
  pagehead *first;
  bufhead *newbuf = NULL, *front, *rear, *temp;

  first = (pagehead*)entry->ptr;
  
  if(buffer->size > size + sizeof(bufhead))
  {
    /* create a new buffer head */
    newbuf = (bufhead*)((long int)buffer + sizeof(bufhead) + size);
    newbuf->size = buffer->size - size -sizeof(bufhead);  
    newbuf->base = (void*)((long int)newbuf + sizeof(bufhead));
    newbuf->prev = (void*)buffer;
    newbuf->next = (void*)buffer->next;
    /* add the new buffer into free list */
    temp = (bufhead*)buffer->next;
    if(temp != NULL)
    	temp->prev = (void*)newbuf;
    buffer->next = (void*)newbuf;
    
    buffer->size = size;
  }

  front = (bufhead*)buffer->prev;
  rear  = (bufhead*)buffer->next;

  if(front == NULL)
  {
    first->freelist = (void*)rear;
    if(rear)
    	rear->prev = NULL;
    return buffer->base;
  }  
  else
  {
    front->next = rear;
    if(rear)
    	rear->prev = (void*)front;
    return buffer->base;
  }
}

void giveback(bufhead *buffer)
{
  pagehead *first;
  bufhead *front, *rear;
  
  first = (pagehead*)entry->ptr;
  /* 1.put it back to the free list */
  front = (bufhead*)first->freelist;
  if(front == NULL)
  {
    first->freelist = buffer;
    buffer->prev = NULL;
    buffer->next = NULL;
  }
  else
  { 
    rear = NULL;
    while(front!=NULL && front < buffer)
    {
      rear = front;
      front = (bufhead*)front->next;
    }

    if(rear == NULL)
    { 
      first->freelist = (void*)buffer;
      buffer->prev = NULL;
      buffer->next = (void*)front;
      front->prev = (void*)buffer;
    }
    else
    {
      buffer->prev = (void*)rear;
      buffer->next = (void*)front;
      rear->next = (void*)buffer;
      if(front != NULL)
      	front->prev = (void*)buffer;
    }
  }
}

void merge(bufhead *buffer)
{
  bufhead *front, *rear, *temp;
  void *reartobuf, *buftofront;
  
  rear = (bufhead*)buffer->prev;   
  front = (bufhead*)buffer->next;

  if(front != NULL)
  {
    buftofront = (void*)((long int)buffer + sizeof(bufhead) + buffer->size);
    if(buftofront == front)
    {
      buffer->size = buffer->size + sizeof(bufhead) + front->size;
      buffer->next = front->next;
      temp = (bufhead*)front->next;
      if(temp != NULL)
      	temp->prev = (void*)buffer;
    } 
  }
  
  if(rear != NULL)
  {
    reartobuf = (void*)((long int)rear + sizeof(bufhead) + rear->size);
    if(reartobuf == buffer)
    {
      rear->size = rear->size + sizeof(bufhead) + buffer->size;
      rear->next = buffer->next;
      temp = (bufhead*)buffer->next;
      if(temp != NULL)
      	temp->prev = (void*)rear;
    }  
  } 
}

#endif // KMA_RM
