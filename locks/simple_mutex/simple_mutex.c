#define _GNU_SOURCE
#include "simple_mutex.h"
#include <stdatomic.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>

static inline int futex(uint32_t *uaddr, int op, uint32_t val,
	const struct timespec *timeout, uint32_t *uaddr2, uint32_t val3)
{
	return syscall(SYS_futex, uaddr, op, val, timeout, uaddr2, val3);
}

static inline int futex_wake(uint32_t *uaddr, int nr_wake)
{
	return futex(uaddr, FUTEX_WAKE, nr_wake, NULL, NULL, 0);
}

static inline int futex_wait(uint32_t *uaddr, int val)
{
	return futex(uaddr, FUTEX_WAIT, val, NULL, NULL, 0);
}

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