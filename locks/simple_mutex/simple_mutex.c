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

// Assumes that mutex is malloc'ed
int simple_mutex_init(simple_mutex_t *mutex)
{
	atomic_init(&mutex->word, _UNLOCKED);
}

// Nothing to do (maybe validate state = unlocked?)
int simple_mutex_destroy(simple_mutex_t *mutex)
{
	(void)mutex;
}

int simple_mutex_lock(simple_mutex_t *mutex)
{
	uint32_t val = _UNLOCKED;
	// Fast path --> not contended (single CAS)
	if (atomic_compare_exchange_strong_explicit(
		&mutex->word, val, _LOCKED,
		memory_order_acquire, memory_order_relaxed))
	{
		return;	// lock acquired
	}

	// CAS failed, val now holds actual word value
	// Slow path --> contended
	// Loop until lock acquired or can successfully sleep
	while (1)
	{
		// Another thread could have already released the lock
		if (val == _UNLOCKED)
		{
			if (atomic_compare_exchange_strong_explicit(
				&mutex->word, val, _LOCKED,
				memory_order_acquire, memory_order_relaxed))
			{
				return;	// lock acquired
			}
			continue; //actual lock state != unlocked
		}
		// Post that there's waiters
		// Implicit release barrier in subsequent syscall
		if (val == _LOCKED)
		{
			if (atomic_compare_exchange_strong_explicit(
				&mutex->word, val, _LOCKED_WAITERS,
				memory_order_relaxed, memory_order_relaxed))
			{
				// CAS succeded, val now holds old value (_LOCKED)
			}
			else continue;
		}
		// Blocking wait. val can be either _LOCKED or _LOCKED_WAITERS
		futex_wait(&mutex->word, val);
		// Reload actual value after waiting (still hasn't got the lock!)
		// Implicit acquire barrier since syscall = full fence
		val = atomic_load_explicit(&mutex->word, memory_order_relaxed);
	}
	
}

int simple_mutex_unlock(simple_mutex_t *mutex)
{
	// unlock
	// check for waiters and call SYS_futex_wake(1)
}