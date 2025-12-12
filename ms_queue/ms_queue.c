#include "ms_queue.h"
#define __need_NULL
#include <stddef.h>

t_ms_queue *create_ms_queue()
{
	t_ms_queue *queue = malloc(sizeof(t_ms_queue));
	if (!queue) return NULL;
	t_node	*dummy = malloc(sizeof(t_node));
	if (!dummy) return (free(queue), NULL);
	dummy->data = NULL;
	atomic_init(&dummy->next, NULL);
	atomic_init(&queue->head, dummy);
	atomic_init(&queue->tail, dummy);
	return queue;
}

// unsafe, leaks if there's data inside,
// user should empty the queue first
// UB if called while concurrently accessed
void destroy_ms_queue(t_ms_queue *q)
{
	if (q)
	{
		if (q->head) free(q->head);
		free(q);
	}
}

bool enqueue(t_ms_queue *q, void *data)
{
	t_node	*new_node = malloc(sizeof(t_node));
	if (!new_node) return false;
	new_node->data = data;
	new_node->next = NULL;
	while (1)
	{
		t_node *tail = atomic_load_explicit(&q->tail, memory_order_acquire);
		t_node *next = atomic_load_explicit(&tail->next, memory_order_acquire);
		if (tail == atomic_load_explicit(&q->tail, memory_order_acquire))
		{
			if (next)	// help advance tail
			{
				atomic_compare_exchange_weak_explicit(
					&q->tail, &tail, next, memory_order_acq_rel, memory_order_relaxed
				);
				continue;
			}
			if (atomic_compare_exchange_weak_explicit(
				&tail->next, &next, new_node, memory_order_release, memory_order_relaxed
			))
			{
				atomic_compare_exchange_strong_explicit(
					&q->tail, &tail, &new_node, memory_order_acq_rel, memory_order_relaxed
				);
				return true;
			}
		}
	}
}

void *dequeue(t_ms_queue *q)
{
	while (1)
	{
		t_node *head = atomic_load_explicit(&q->head, memory_order_acquire);
		t_node *tail = atomic_load_explicit(&q->tail, memory_order_acquire);
		t_node *next = atomic_load_explicit(&head->next, memory_order_acquire);
		if(head == atomic_load_explicit(&q->head, memory_order_acquire))
		{
			if (head == tail)
			{
				if (!next)	return NULL; // empty
				atomic_compare_exchange_weak_explicit(
					&q->tail, &tail, next, memory_order_acq_rel, memory_order_relaxed
				);
				continue;
			}
			void *value = next->data;
			if (atomic_compare_exchange_weak_explicit(
				&q->head, &head, next, memory_order_acq_rel, memory_order_relaxed
			))
			{
				// UB if someone is accessing the node, .data leaks if the user does not free it
				free(head);
				return value;
			}
		}
	}
}