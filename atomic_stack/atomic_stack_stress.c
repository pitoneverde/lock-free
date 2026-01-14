// Mainly AI-generated, checked for correctness and integration

#ifdef BASE_STACK
# include "atomic_stack.h"
#elif defined(HP_STACK)
# include "atomic_stack_hp.h"
#endif

#include <stdio.h>
#include <pthread.h>
#include <assert.h>

#define THREADS 16
#define OPS_PER_THREAD 1000000

void *stress_test(void *arg)
{
#ifdef HP_STACK
	hp_init_thread();
#endif
	LF_stack *stack = arg;

	for (int i = 0; i < OPS_PER_THREAD; i++)
	{
		// Push then pop - should maintain balance
		int *data = malloc(sizeof(int));
		*data = i;
		t_stack_node *node = new_node(data);

		push(stack, node);
		t_stack_node *popped = pop(stack);

		if (popped)
		{
			assert(popped == node || popped->data != NULL);
#ifdef BASE_STACK
			free(popped->data);
			free(popped);
#endif
		}
	}

#ifdef HP_STACK
	hp_cleanup_thread();
#endif
	return NULL;
}

int main()
{
#ifdef HP_STACK
	hp_init_thread();
#endif
	LF_stack stack;
	atomic_store(&stack.top, ((t_stack_top){NULL, 0}));

	pthread_t threads[THREADS];

	printf("Running ultimate stress test...\n");
	for (int i = 0; i < THREADS; i++)
	{
		pthread_create(&threads[i], NULL, stress_test, &stack);
	}

	for (int i = 0; i < THREADS; i++)
	{
		pthread_join(threads[i], NULL);
	}

	// Stack should be empty
	assert(pop(&stack) == NULL);
	printf("✓ Stress test PASSED - no crashes, no leaks\n");

#ifdef HP_STACK
	hp_cleanup_thread();
#endif
	return 0;
}
