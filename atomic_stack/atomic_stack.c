/*
abstract lock-free stack pattern
push(node):
	node.next = head
	while !atomic_update(head, node.next, node):
		node.next = head

pop():
	while true:
		old_head = head
		if old_head == null: return null
		new_head = old_head.next
		if atomic_update(head, old_head, new_head):
			return old_head
*/
// start API
#include "atomic_stack.h"

// 
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
		if (current.node == NULL) return NULL;
		next.node = current.node->next;
		next.version = current.version + 1;
	} while (!atomic_compare_exchange_strong(&stack->top, &current, next));
	return current.node;
}

t_stack_node *new_node(void *data)
{
	t_stack_node *node = malloc(sizeof(t_stack_node));
	if (!node) return NULL;
	node->data = data;
	return node;
}
// end API

// comprehensive test: atomic_stack_test.c
// stress test: atomic_stack_stress.c
// basic test: uncomment below, then:
// cc -std=c11 ./atomic_stack.c -O2 -mcx16 -latomic -o atomic_stack

// t_stack_node *create_new_node(void *data)
// {
// 	t_stack_node *node = new_node(data);
// 	if (!node) exit(1);
// 	return node;
// }
// void	push_data(LF_stack *stack, void *data)
// {
// 	t_stack_node *node = create_new_node(data);
// 	push(stack, node);
// }

// int main()
// {
// 	int a = 1, b = 2, c = 3;
// 	LF_stack *stack = malloc(sizeof(LF_stack));
// 	if (!stack) return 1;
// 	stack->top = NULL;
// 	push_data(stack, &a);
// 	push_data(stack, &b);
// 	push_data(stack, &c);
	
// 	return 0;
// }
