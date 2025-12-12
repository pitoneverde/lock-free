#include "spsc_ring.h"
#include <string.h>
#include <math.h>

// stolen from linux kfifo, roundups to multiples of 64
static inline size_t to_cache_size(size_t n)
{
	n--;
	n |= n >> 1;
	n |= n >> 2;
	n |= n >> 4;
	n |= n >> 8;
	n |= n >> 16;
	n |= n >> 32;
	return n + 1;
}

t_spsc_ring *spsc_create(size_t size)
{
	// in c99 is posix_memalign
	t_spsc_ring	*ring = aligned_alloc(64, sizeof(t_spsc_ring));
	if (!ring) return NULL;
	if (size < 2) size = 2;
	size = to_cache_size(size);
	ring->buf = aligned_alloc(64, size);
	if (!ring->buf)
		return (free(ring), NULL);
	ring->size = size;
	ring->mask = size - 1;
	atomic_init(&ring->head, 0);
	atomic_init(&ring->tail, 0);
	return ring;
}

void spsc_destroy(t_spsc_ring *r)
{
	if (r)
	{
		if (r->buf) free(r->buf);
		free(r);
	}
}

bool spsc_try_push(t_spsc_ring *r, unsigned char byte)
{
	size_t curr_tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
	size_t curr_head = atomic_load_explicit(&r->head, memory_order_acquire);
	if ((curr_tail - curr_head) >= r->mask)
		return false;		//  buffer full
	r->buf[curr_tail & r->mask] = byte;
	atomic_store_explicit(&r->tail, curr_tail + 1, memory_order_release);
	return true;
}

bool spsc_try_pop(t_spsc_ring *r, unsigned char *byte)
{
	size_t curr_head = atomic_load_explicit(&r->head, memory_order_relaxed);
	size_t curr_tail = atomic_load_explicit(&r->tail, memory_order_acquire);
	if (curr_head == curr_tail)
		return false;		// buffer empty
	(*byte) = r->buf[curr_head & r->mask];
	atomic_store_explicit(&r->head, curr_head + 1, memory_order_release);
	return true;
}

size_t spsc_push_batch(t_spsc_ring *r, const void *rawdata, size_t count)
{
	size_t curr_tail = atomic_load_explicit(&r->tail, memory_order_relaxed);
	size_t curr_head = atomic_load_explicit(&r->head, memory_order_acquire);
	size_t free_space = r->mask - (curr_head - curr_tail);		// 1 cell wasted as guard
	size_t to_push = (count < free_space) ? count : free_space;		// min
	if (!to_push) return 0;
	size_t w_idx = r->tail & r->mask;
	size_t chunk = r->size - w_idx;
	if (to_push <= chunk)
		memcpy(r->buf + w_idx, rawdata, to_push);
	else {		// buffer wrap-around
		memcpy(r->buf + w_idx, rawdata, chunk);
		memcpy(r->buf, rawdata + chunk, to_push - chunk);
	}
	atomic_store_explicit(&r->tail, curr_tail + to_push, memory_order_release);
	return to_push;
}

size_t spsc_pop_batch(t_spsc_ring *r, void *rawdata, size_t count)
{
	size_t curr_head = atomic_load_explicit(&r->head, memory_order_relaxed);
	size_t curr_tail = atomic_load_explicit(&r->tail, memory_order_acquire);
	size_t available = curr_tail - curr_head;
	size_t to_pop = (count < available) ? count : available;
	if (!to_pop) return 0;
	size_t r_idx = r->head & r->mask;
	size_t chunk = r->size - r_idx;
	if (to_pop <= chunk)
		memcpy(rawdata, r->buf + r_idx, to_pop);
	else {
		memcpy(rawdata, r->buf + r_idx, chunk);
		memcpy(rawdata + chunk, r->buf, to_pop - chunk);
	}
	atomic_store_explicit(&r->head, curr_head + to_pop, memory_order_release);
	return to_pop;
}
