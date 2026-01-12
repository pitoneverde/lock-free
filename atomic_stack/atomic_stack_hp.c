#include "atomic_stack_hp.h"

void push(LF_stack* stack, t_stack_node *new_node)
{
	t_stack_top current, next;
	do {
	current = atomic_load(&stack->top);
	new_node->next = current.node;
	next.node = new_node;
	next.version = current.version + 1;
	} while (!atomic_compare_exchange_strong(&stack->top, &current, next));
}

t_stack_node *pop(LF_stack *stack)
{
	t_stack_top current, next;
	do {
		current = atomic_load(&stack->top);
		if (!current.node) return NULL;
		hp_protect(0, current.node);
		next.node = current.node->next;
		next.version = current.version + 1;
	} while (!atomic_compare_exchange_strong(&stack->top, &current, next));
	hp_clear(0);
	hp_retire(current.node);
	return current.node;
}

t_stack_node *new_node(void *data)
{
	t_stack_node *node = malloc(sizeof(t_stack_node));
	if (!node) return NULL;
	node->data = data;
	return node;
}