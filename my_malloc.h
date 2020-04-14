#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

struct linkedSpace {
  size_t size;
  bool available;  //if available is true, it is freed. Otherwise, it is allocated already
  struct linkedSpace * free;     //next freed block
  struct linkedSpace * preFree;  //previous freed block
} typedef linkedSpace;

linkedSpace * allocNew(size_t size);
void split(size_t size, linkedSpace * tmp);
void merge(linkedSpace * next, linkedSpace * ls);
void connectNew(linkedSpace * tmp2);
void * ts_malloc_lock(size_t size);
void ts_free_lock(void * ptr);
unsigned long get_data_segment_size();
unsigned long get_data_segment_free_space_size();
void mergeTLS(linkedSpace * next, linkedSpace * ls);
void connectNewTLS(linkedSpace * tmp2);
void * ts_malloc_nolock(size_t size);
void ts_free_nolock(void * ptr);
