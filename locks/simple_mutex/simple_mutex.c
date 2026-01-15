#ifndef _GNU_SOURCE
# define _GNU_SOURCE
#endif

#include "simple_mutex.h"
#include <stdatomic.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <errno.h>
#include <assert.h>

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

// Assumes that mutex is already allocated (malloc or stack)
int simple_mutex_init(simple_mutex_t *mutex)
{
	if (!mutex)
		return -EINVAL;
	atomic_init(&mutex->word, _UNLOCKED);
	return 0;
}

// Validate state and poison the state to catch use-after-free
int simple_mutex_destroy(simple_mutex_t *mutex)
{
	if (!mutex)
		return -EINVAL;
	// Check if locked or invalid, following pthread style
	uint32_t val = atomic_load_explicit(&mutex->word, memory_order_relaxed);
	if (val == 0xDEADBEEF)
		return -EINVAL;
	if (val != _UNLOCKED)
		return -EBUSY;
	// Poison the state
	atomic_store_explicit(&mutex->word, 0xDEADBEEF, memory_order_relaxed);
	return 0;
}

// return 0 on success, -1 on syscall failure (which sets errno)
int simple_mutex_lock(simple_mutex_t *mutex)
{
	if (!mutex)
		return -EINVAL;
	// Safety check --> don't try to lock destroyed mutex
	uint32_t val = atomic_load_explicit(&mutex->word, memory_order_acquire);
	if (val == 0xDEADBEEF)
		return -EINVAL;
	val = _UNLOCKED;
	// Fast path --> not contended (single CAS)
	if (atomic_compare_exchange_strong_explicit(
		&mutex->word, &val, _LOCKED,
		memory_order_acquire, memory_order_relaxed))
	{
		return 0;	// lock acquired
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
				&mutex->word, &val, _LOCKED,
				memory_order_acquire, memory_order_relaxed))
			{
				return 0;	// lock acquired
			}
			continue; //actual lock state != unlocked
		}
		// Post that there's waiters
		// Implicit release barrier in subsequent syscall
		if (val == _LOCKED)
		{
			// necessary because CAS modifies val with actual state on failure
			uint32_t old_val = val;
			if (atomic_compare_exchange_strong_explicit(
				&mutex->word, &old_val, _LOCKED_WAITERS,
				memory_order_relaxed, memory_order_relaxed))
			{
				// CAS succeded, val now holds old value (_LOCKED)
				val = _LOCKED_WAITERS;
			}
			else
			{
				val = old_val;	// current
				continue;
			}
		}
		// Blocking wait. val can be either _LOCKED or _LOCKED_WAITERS
		int rc = futex_wait(&mutex->word, val);
		if (rc == -1)	// = wait failed
		{
			int err = errno;
			// Expected in case of race condition before sleep (safeguard, must retry)
			// Signal handler interrupted the wait (retry, maybe later add retry limit for robustness)
			if (err == EAGAIN || err == EINTR)
			{
				val = atomic_load_explicit(&mutex->word, memory_order_relaxed);
				continue;
			}
			// Fatal error, like EINVAL, EACCESS...
			return -err;
		}
		// Probably woken with rc >= 0
		// Reload actual value after waiting and retry (still hasn't got the lock!)
		// Implicit acquire barrier since syscall = full fence
		val = atomic_load_explicit(&mutex->word, memory_order_relaxed);
	}
	return 0; // unreachable
	
}

// return 0 on success, -errno on syscall failure
int simple_mutex_unlock(simple_mutex_t *mutex)
{
	if (!mutex)
		return -EINVAL;
	// Safety check --> don't try to unlock destroyed mutex
	uint32_t val = atomic_load_explicit(&mutex->word, memory_order_acquire);
	if (val == 0xDEADBEEF)
		return -EINVAL;
	// unlock with release to ensure visibility exiting critical section
	uint32_t prev_val = atomic_exchange_explicit(
		&mutex->word, _UNLOCKED,
		memory_order_release
	);
	// syscall only if there's waiters (skip if low contention)
	if (prev_val == _LOCKED_WAITERS)
	{
		int rc = futex_wake(&mutex->word, 1);
		if (rc == -1)	// Catastrophic failure
			return -errno;
		// assert(rc == 0);
	}
	return 0;
}