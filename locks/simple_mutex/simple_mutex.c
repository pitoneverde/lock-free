#define _GNU_SOURCE
#include "simple_mutex.h"
#include <stdatomic.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <pthread.h>

// assumes that mutex is malloc'ed
int simple_mutex_init(simple_mutex_t *mutex)
{
	atomic_init(&mutex->word, 0);
}

// nothing to do (maybe validate state = unlocked?)
int simple_mutex_destroy(simple_mutex_t *mutex)
{
	(void)mutex;
}

int simple_mutex_lock(simple_mutex_t *mutex)
{
	// fast path --> not contended
	if (atomic_compare_exchange_strong_explicit(
		&mutex->word, _UNLOCKED, _LOCKED, 
		memory_order_acquire, memory_order_relaxed))
	{
		return;	// lock acquired
	}

	// slow path --> contended
	// syscall(SYS_futex, FUTEX_WAIT)
	// SYS_futex_wait
}

int simple_mutex_unlock(simple_mutex_t *mutex)
{
	// unlock
	// check for waiters and call SYS_futex_wake(1)
}