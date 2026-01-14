#ifndef SIMPLE_MUTEX_H
# define SIMPLE_MUTEX_H

#include <stdalign.h>
#include <stdint.h>

#define _UNLOCKED 0
#define _LOCKED 1
#define _HAS_WAITERS 2
#define _LOCKED_WAITERS 3  // _LOCKED << 1

/*
 * Important note: this is the base lock (TAS, test-and-set).
 * In production code it's terribly slow (it syscalls in non-zero contention).
 * 
 * Limitations: non-reentrant, no owner check. 
 * Calling unlock() from a thread that doesn't own the lock causes undefined behaviour,
 * including, but not limited to: data corruption, data races, deadlocks, checker errors.
 * Such behaviour is not a bug, it's a feature (and is a programmer skill issue).
 *  
 * See the adaptive version for a more useful implementation:
 * with some tuning it should perform like pthread_mutex_t
*/
typedef struct
{
	alignas(64) uint32_t word;
} simple_mutex_t;

#define SIMPLE_MUTEX_INITIALIZER {0}

int simple_mutex_init(simple_mutex_t *mutex);
int simple_mutex_destroy(simple_mutex_t *mutex);

// Can deadlock if called twice on same mutex by same thread
int simple_mutex_lock(simple_mutex_t *mutex);
int simple_mutex_unlock(simple_mutex_t *mutex);

// int simple_mutex_trylock(simple_mutex_t *mutex);
#endif