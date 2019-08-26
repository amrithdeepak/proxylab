/*
 * cache - A simple linked-list implementation of a cache
 *         It is threadsafe.
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
#include "cache.h"

/* cache */
int total_size;
int num;
int pc;

cb_t* end;

/* A read write lock to protect the cache */
pthread_rwlock_t cache_lock;

/* Initializes default variables of the cache */
void init_cache()
{
	/* Init cache var */
	total_size = 0;
	num = 0;
	end = NULL;
	pc = 0;
	
	/* Init rwlock */
	if (pthread_rwlock_init(&cache_lock, NULL))
	{
	    printf("Failed to initialize rw lock.\n");
	    exit(0);
	}
}

/* Free cache block */
void free_cb(cb_t* cb)
{
	/* Free inside pointers */
	Free(cb->hostname);
	Free(cb->uri);
	Free(cb->data);

	/* Free pointer itself */
	Free(cb);
}

/* Get total_size of cache */
int get_total_size()
{
	return total_size;
}

/* Get cache lock */
pthread_rwlock_t* get_cache_lock()
{
	return &cache_lock;
}

/* 
 * remove_LRU: Removes the most recently used element 
 * in accordance with the access counter pc 
 */
void remove_LRU()
{
	/* Check empty cache */
	if(end == NULL) 
		return;

	/* Find min use_index */
	cb_t* curr = end;
	cb_t* min_p = curr;
	uint32_t min = curr->use_index;
	curr = curr->next;
	
	/* Iterate through the linked list 
	   By construction, no pointers should be 
	   NULL */
	while(curr != end)
	{
		/* Still safeguard */
		if(curr != NULL && curr->use_index < min)
		{
			/* Set minimum */
			min_p = curr;
			min = curr->use_index;
		}
		curr = curr->next;
	}

	/* Update size and num */
	total_size -= min_p->size;
	num--;

	/* update pointers: this is fine since it is
	   circularly linked, so no edge cases */
	min_p->prev->next = min_p->next;
	min_p->next->prev = min_p->prev;

	/* Free pointer */
	free_cb(min_p);
}

/* 
 * update: updates the use_index of a cache_block
 * which is used in determining LRU
 */
void update(cb_t* cb)
{
	/* This is purposefully left thread-unsafe 
	   to allow multireads */
	/* The downside is that LRU is not exact, but
	   approximate. */
	cb->use_index = pc++;
}

/* 
 * add_elem: Add element to the cache 
 */
void add_elem(char *hostname, char *uri, char *data)
{
	int size = 0;

	/* Lock while writing to cache */
	if (pthread_rwlock_wrlock(&cache_lock))
	{
	    printf("Failed to get a write lock.\n");
	    exit(0);
	}

	/* Allocate new cache block */
	cb_t *cb = Malloc(sizeof(cb_t));

	/* Allocate space for data */
	/* The plus one is for null character */
	size += strlen(hostname)+1;
	char* hn = Malloc(strlen(hostname)+1);
	strcpy(hn, hostname);
	
	size += strlen(uri)+1;
	char* u = Malloc(strlen(uri)+1);
	strcpy(u, uri);

	size += strlen(data)+1;
	char* d = Malloc(strlen(data)+1);
	strcpy(d, data);

	/* Update params */
	cb->hostname = hn;
	cb->uri = u;
	cb->data = d;

	/* Make space in cache */
	while(total_size + size >= MAX_CACHE_SIZE)
		remove_LRU();

	/* If first elem then */
	if(num == 0)
	{	
		cb->prev = cb;
		cb->next = cb;
	}
	else
	{
		/* Otherwise add to end of LL */
		cb->prev = end;
		cb->next = end->next;
		end->next = cb;
	}

	/* Update cache params */
	end = cb;
	num++;
	cb->size = size;
	cb->use_index = pc++;
	total_size += size;

	/* Unlock cache */
	if (pthread_rwlock_unlock(&cache_lock))
	{
	    printf("Failed tounlock a write lock.\n");
	    exit(0);
	}
}

/* 
 * find: finds a specific hostname,uri pair in the cache 
 *       and returns a pointer to that cache block, or
 *       NULL otherwise
 */
cb_t* find(char* hostname, char* uri)
{
	/* Create read lock */
  	if (pthread_rwlock_rdlock(&cache_lock))
	{
	    printf("Failed to get a  read lock.\n");
	    exit(0);
	}

	/* Empty cache */
	if(end == NULL) 
	{
	    if (pthread_rwlock_unlock(&cache_lock))
	    {
			printf("Failed to unlock a  read lock.\n");
			exit(0);
	    }
	    return NULL;
	}

	/* Check first elem of cache */
	cb_t* curr = end;
	if(strcmp(hostname, curr->hostname) == 0 && strcmp(uri, curr->uri)==0)
		return curr;

	/* Check rest of cache 
	   All pointers should be non-NULL */
	curr = curr -> next;

	/* Still safeguard */
	while(curr != NULL && curr != end)
	{
		if(strcmp(hostname, curr->hostname) == 0 && strcmp(uri, curr->uri)==0)
			return curr;
		curr = curr->next;
	}

	/* Unlock cache */
	if (pthread_rwlock_unlock(&cache_lock))
	{
	    printf("Failed to unlock a  read lock.\n");
	    exit(0);
	}
	return NULL;
}