#define _POSIX_C_SOURCE 200809L // For clock_gettime

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

// Descriptor with tagged pointer size=16B
typedef struct s_stack_top
{
	t_stack_node *node;
	unsigned long version;
} t_stack_top;

// 16-byte aligned for 128 bit atomic ops
// Descriptors are stack allocated to improve performance in hot paths
// also for portability and cache-friendly data
typedef struct LF_stack
{
	_Alignas(16) _Atomic(t_stack_top) top;
} LF_stack;

void push(LF_stack *stack, t_stack_node *new_node);
t_stack_node *pop(LF_stack *stack);
t_stack_node *new_node(void *data);

#endif