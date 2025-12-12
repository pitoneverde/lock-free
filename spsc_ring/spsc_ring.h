#ifndef SPSC_RING_H
#define SPSC_RING_H

#include <stdlib.h>
#include <stdbool.h>
#include <stdalign.h>
#include <stdatomic.h>

// head and tail aligned as 64B 
// to take a full cache line and avoid false sharing
struct spsc_ring
{
	alignas(64)
	atomic_size_t head;
	char _head_padding[64 - sizeof(atomic_size_t)];

	alignas(64)
	atomic_size_t tail;
	char _tail_padding[64 - sizeof(atomic_size_t)];

	unsigned char	*buf;
	size_t	size;
	size_t	mask;
};

// Minimal API
typedef struct spsc_ring t_spsc_ring;

t_spsc_ring *spsc_create(size_t size);
void spsc_destroy(t_spsc_ring *r);

bool spsc_try_push(t_spsc_ring *r, unsigned char byte);
bool spsc_try_pop(t_spsc_ring *r, unsigned char *byte);

size_t spsc_push_batch(t_spsc_ring *r, const void *rawdata, size_t count);
size_t spsc_pop_batch(t_spsc_ring *r, void *rawdata, size_t count);

#endif