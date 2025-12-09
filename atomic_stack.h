#define _POSIX_SOURCE
#ifndef ATOMIC_STACK_H
#define ATOMIC_STACK_H
#include <stdatomic.h>
#include <unistd.h>
#include <stdlib.h>

typedef struct s_stack_node t_stack_node;

struct s_stack_node
{
	void *data;
	t_stack_node *next;
};

typedef struct LF_stack
{
	_Atomic(t_stack_node *) top;
} LF_stack;

void push(LF_stack *stack, t_stack_node *new_node);
t_stack_node *pop(LF_stack *stack);
t_stack_node *new_node(void *data);

#endif