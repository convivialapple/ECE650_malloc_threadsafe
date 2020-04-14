
#include "my_malloc.h"

#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
__thread linkedSpace * initialTLS = NULL;  //the head of freeList for each thread
static linkedSpace * initialFree = NULL;   //the static head for all of threads
static unsigned long total = 0;            //track newly allocated space on heap
static unsigned long freed = 0;            //track freed space

pthread_mutex_t sbrk_locker = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t locker = PTHREAD_MUTEX_INITIALIZER;

/*This function is to request more memory from system 
  when there is no suitable freed block in freeList.*/
linkedSpace * allocNew(size_t size) {
  pthread_mutex_lock(&sbrk_locker);
  linkedSpace * ls = (linkedSpace *)sbrk(0);
  total += (size + sizeof(linkedSpace));
  size_t realSize = size + sizeof(linkedSpace);
  if (sbrk(realSize) == (void *)-1) {  //there is no enough space
    pthread_mutex_unlock(&sbrk_locker);
    return NULL;
  }
  pthread_mutex_unlock(&sbrk_locker);
  ls->size = size;
  ls->available = false;
  ls->free = NULL;
  return ls;
}

/*When the searched freed block is too large to be occupied, 
it will split the freed block into two pieces. It sets the first 
piece as freed block and the second piece as allocated block*/
void split(size_t size, linkedSpace * tmp) {
  size_t tmpSize = tmp->size;
  tmp->size = (tmpSize - size - sizeof(linkedSpace));
  linkedSpace * splitted = (linkedSpace *)((char *)tmp + tmp->size + sizeof(linkedSpace));
  splitted->size = size;
  splitted->free = NULL;
  freed -= (size + sizeof(linkedSpace));
}

/*This function is to merge two adjacent freed blocks to avoid fragmentization*/
void merge(linkedSpace * cur, linkedSpace * ls) {
  linkedSpace * next = cur->free;
  if (initialFree == next) {
    initialFree = ls;
  }
  //replaces this next block with current freed block and increases current freed block’s size

  ls->free = next->free;
  cur->free = ls;
  ls->size = ls->size + next->size + sizeof(linkedSpace);
  ls->available = true;
  next->available = false;
  next->free = NULL;
}

/*This function have same functionality with merge(). Differenctly,
  it uses thread local variables */
void mergeTLS(linkedSpace * cur, linkedSpace * ls) {
  linkedSpace * next = cur->free;
  if (initialTLS == next) {
    initialTLS = ls;
  }
  //replaces this next block with current freed block and increases current freed block’s size

  ls->free = next->free;
  cur->free = ls;
  ls->size = ls->size + next->size + sizeof(linkedSpace);
  ls->available = true;
  next->available = false;
  next->free = NULL;
}

/* This function is to remove the block from the freeList*/
void connectNew(linkedSpace * tmp2) {
  linkedSpace * toDelete = tmp2->free;
  freed -= (toDelete->size + sizeof(linkedSpace));
  toDelete->available = false;
  // It updates the preFree of the nextFree block and the nextFree of the preFree block.
  tmp2->free = toDelete->free;

  if (initialFree == toDelete) {
    initialFree = toDelete->free;
  }

  //preFree and nextFree blocks of itself are set to NULL
  toDelete->free = NULL;
}

/*This function have same functionality with connectNewTLS(). Differenctly,
  it uses thread local variables */
void connectNewTLS(linkedSpace * tmp2) {
  linkedSpace * toDelete = tmp2->free;
  freed -= (toDelete->size + sizeof(linkedSpace));
  toDelete->available = false;
  // It updates the preFree of the nextFree block and the nextFree of the preFree block.
  tmp2->free = toDelete->free;

  if (initialTLS == toDelete) {
    initialTLS = toDelete->free;
  }

  //preFree and nextFree blocks of itself are set to NULL
  toDelete->free = NULL;
}

void * ts_malloc_lock(size_t size) {
  int n = 0;
  int fit = 0;
  size_t minSize = INT_MAX;
  pthread_mutex_lock(&locker);
  linkedSpace * tmp = initialFree;
  //Traverse through the whole freeList
  // It compares, updates and stores the most fitted freed block constantly.
  while (tmp != NULL && tmp->free != NULL) {
    if (tmp->free->size == size) {
      linkedSpace * toDelete = tmp->free;
      connectNew(tmp);
      pthread_mutex_unlock(&locker);
      return (char *)toDelete + sizeof(linkedSpace);
    }
    else if (tmp->free->size > size && minSize > tmp->free->size) {
      minSize = tmp->free->size;
      fit = n;
    }
    n++;
    tmp = tmp->free;
  }

  if (minSize == INT_MAX) {
    void * address = (char *)allocNew(size) + sizeof(linkedSpace);
    pthread_mutex_unlock(&locker);
    return address;
  }

  linkedSpace * tmp2 = initialFree;
  for (int i = 0; i < fit; i++) {
    tmp2 = tmp2->free;
  }
  //if there is still remaining size, split it
  linkedSpace * ans = tmp2;
  if (tmp2->free->size > 1 * (size + sizeof(linkedSpace))) {
    split(size, tmp2->free);
    ans = (linkedSpace *)((char *)tmp2->free + tmp2->free->size + sizeof(linkedSpace));
  }
  else {
    ans = tmp2->free;
    connectNew(tmp2);
  }
  pthread_mutex_unlock(&locker);
  return (char *)ans + sizeof(linkedSpace);
}

/*The main job of this function is to connect newly freed block
into freeList and update head */
void ts_free_lock(void * ptr) {
  pthread_mutex_lock(&locker);
  linkedSpace * ls = (linkedSpace *)((char *)ptr - sizeof(linkedSpace));
  if (ls->available == true) {
    pthread_mutex_unlock(&locker);
    return;
  }
  freed += ls->size + sizeof(linkedSpace);

  linkedSpace * next = (linkedSpace *)((char *)ls + ls->size + sizeof(linkedSpace));

  linkedSpace * cur = initialFree;
  if (next < (linkedSpace *)sbrk(0) && next->available == true && next != initialFree) {
    while (cur != NULL && cur->free != NULL && cur->free != next) {
      cur = cur->free;
    }
    if (ls != NULL && cur != NULL && cur->free != NULL) {
      merge(cur, ls);
    }
    else {
      ls->available = true;
      ls->free = initialFree;
      initialFree = ls;
    }
  }
  else {
    ls->available = true;
    ls->free = initialFree;
    initialFree = ls;
  }
  pthread_mutex_unlock(&locker);
}

void * ts_malloc_nolock(size_t size) {
  linkedSpace * tmp = initialTLS;
  int n = 0;
  int fit = 0;
  size_t minSize = INT_MAX;
  //Traverse through the whole freeList
  // It compares, updates and stores the most fitted freed block constantly.
  while (tmp != NULL && tmp->free != NULL) {
    if (tmp->free->size == size) {
      linkedSpace * toDelete = tmp->free;
      connectNewTLS(tmp);
      return (char *)toDelete + sizeof(linkedSpace);
    }
    else if (tmp->free->size > size && minSize > tmp->free->size) {
      minSize = tmp->free->size;
      fit = n;
    }
    n++;
    tmp = tmp->free;
  }

  if (minSize == INT_MAX) {
    return (char *)allocNew(size) + sizeof(linkedSpace);
  }

  linkedSpace * tmp2 = initialTLS;
  for (int i = 0; i < fit; i++) {
    tmp2 = tmp2->free;
  }
  //if there is still remaining size, split it
  linkedSpace * ans = tmp2;
  if (tmp2->free->size > 1 * (size + sizeof(linkedSpace))) {
    split(size, tmp2->free);
    ans = (linkedSpace *)((char *)tmp2->free + tmp2->free->size + sizeof(linkedSpace));
  }
  else {
    ans = tmp2->free;
    connectNewTLS(tmp2);
  }
  return (char *)ans + sizeof(linkedSpace);
}

//free
void ts_free_nolock(void * ptr) {
  linkedSpace * ls = (linkedSpace *)((char *)ptr - sizeof(linkedSpace));
  if (ls->available == true) {
    return;
  }
  freed += ls->size + sizeof(linkedSpace);

  linkedSpace * next = (linkedSpace *)((char *)ls + ls->size + sizeof(linkedSpace));

  linkedSpace * cur = initialTLS;
  if (next < (linkedSpace *)sbrk(0) && next->available == true && next != initialTLS) {
    while (cur != NULL && cur->free != NULL && cur->free != next) {
      cur = cur->free;
    }
    if (ls != NULL && cur != NULL && cur->free != NULL) {
      mergeTLS(cur, ls);
    }
    else {
      ls->available = true;
      ls->free = initialTLS;
      initialTLS = ls;
    }
  }
  else {
    ls->available = true;
    ls->free = initialTLS;
    initialTLS = ls;
  }
}

//Return a global variable which tracks of newly allocated space on heap
unsigned long get_data_segment_size() {
  return total;
}
//Return a global varialbe which tracks of freed space
unsigned long get_data_segment_free_space_size() {
  return freed;
}
