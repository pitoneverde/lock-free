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

void push(LF_stack* stack, t_stack_node *new_node)
{
	t_stack_node *top;
	do {
	top = atomic_load(&stack->top);
	new_node->next = top;
	} while (!atomic_compare_exchange_strong(&stack->top, &top, new_node));
}

// may spin multiple times
t_stack_node *pop(LF_stack *stack)
{
	while (1)
	{
		t_stack_node *old_head = atomic_load(&stack->top);
		if (old_head == NULL) return NULL;
		t_stack_node *new_head = old_head->next;
		if (atomic_compare_exchange_strong(&stack->top, &old_head, new_head))
			return old_head;
	}
}

t_stack_node *new_node(void *data)
{
	t_stack_node *node = malloc(sizeof(t_stack_node));
	if (!node) return NULL;
	node->data = data;
	return node;
}
// end API

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

// basic test
// cc -std=c11 ./atomic_stack.c -O2 -mcx16 -latomic -o atomic_stack
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

// comprehensive test: atomic_stack_test.c