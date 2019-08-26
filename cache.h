/*
 * cache.h - A simple linked-list implementation of a cache
 *           It is threadsafe.
 *
 * Sunny Nahar
 * anahar
 *
 * Amrith Deepak
 * amrithd
 */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "csapp.h"

/* Recommended max cache and object sizes */
#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400

/* cache block struct */
typedef struct cache_block
{
	uint32_t use_index;
	uint32_t size;
	char* hostname;
	char* uri;
	char* data;
	struct cache_block* prev;
	struct cache_block* next;
} cb_t;

void init_cache();
void free_cb(cb_t* cb);
int get_total_size();
pthread_rwlock_t* get_cache_lock();
void remove_LRU();
void update(cb_t* cb);
void add_elem(char *hostname, char *uri, char *data);
cb_t* find(char* hostname, char* uri);