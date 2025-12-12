#ifndef MS_QUEUE_H
#define MS_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>

typedef struct node t_node;

struct node
{
	void *data;
	_Atomic(t_node *) next;
};

typedef struct ms_queue
{
	_Atomic(t_node *) head;
	_Atomic(t_node *) tail;
} t_ms_queue;

t_ms_queue	*create_ms_queue();
void	destroy_ms_queue(t_ms_queue *q);

bool	enqueue(t_ms_queue *q, void *data);	
void	*dequeue(t_ms_queue *q);

#endif