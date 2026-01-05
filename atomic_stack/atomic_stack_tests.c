// DISCLAIMER: tests are mostly Ai-generated cause it's boring

#ifdef BASE_STACK
# include "atomic_stack.h"
#elif defined(HP_STACK)
# include "atomic_stack_hp.h"
#endif

#include <stdio.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>

#define NUM_THREADS 8
#define OPERATIONS_PER_THREAD 10000
#define VALUE_RANGE 1000

// Thread arguments structure
typedef struct {
    LF_stack *stack;
    int thread_id;
    int pushes;
    int pops;
} thread_args;

// Global statistics
_Atomic int total_pushes = 0;
_Atomic int total_pops = 0;
_Atomic int successful_pops = 0;
_Atomic int failed_pops = 0;

// Thread function - no memory leaks
void *test_mixed_operations(void *arg) {
    thread_args *args = (thread_args *)arg;
    LF_stack *stack = args->stack;
    
    unsigned int seed = (unsigned int)(time(NULL) ^ pthread_self());
    
    for (int i = 0; i < OPERATIONS_PER_THREAD; i++) {
        if (rand_r(&seed) % 2) {
            // Push operation
            int *value = malloc(sizeof(int));
            *value = (args->thread_id * VALUE_RANGE) + (rand_r(&seed) % VALUE_RANGE);
            
            t_stack_node *node = new_node(value);
            if (node) {
                push(stack, node);
                atomic_fetch_add(&total_pushes, 1);
                args->pushes++;
            }
        } else {
            // Pop operation
            t_stack_node *node = pop(stack);
            if (node) {
                // Clean up the node
                if (node->data) free(node->data);
                free(node);
                
                atomic_fetch_add(&successful_pops, 1);
                args->pops++;
            } else {
                atomic_fetch_add(&failed_pops, 1);
            }
            atomic_fetch_add(&total_pops, 1);
        }
    }
    
    return NULL;
}

// Drain stack helper - frees all remaining nodes
void drain_stack(LF_stack *stack) {
    t_stack_node *node;
    while ((node = pop(stack)) != NULL) {
        if (node->data) free(node->data);
        free(node);
    }
}

int main() {
    printf("=== Lock-Free Stack Concurrent Test ===\n");
    
    // Initialize stack
    LF_stack *stack = malloc(sizeof(LF_stack));
    if (!stack) return 1;
	t_stack_top init = { .node = NULL, .version = 0 };
    atomic_store(&stack->top, init);
    
    pthread_t threads[NUM_THREADS];
    thread_args args[NUM_THREADS];
    
    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);
    
    printf("Test 1: Starting %d threads with mixed operations...\n", NUM_THREADS);
    
    // Create threads
    for (int i = 0; i < NUM_THREADS; i++) {
        args[i].stack = stack;
        args[i].thread_id = i;
        args[i].pushes = 0;
        args[i].pops = 0;
        
        if (pthread_create(&threads[i], NULL, test_mixed_operations, &args[i]) != 0) {
            perror("Failed to create thread");
            return 1;
        }
    }
    
    // Wait for all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    clock_gettime(CLOCK_MONOTONIC, &end);
    
    // Calculate statistics
    double elapsed = (end.tv_sec - start.tv_sec) + 
                    (end.tv_nsec - start.tv_nsec) / 1e9;
    
    int total_thread_pushes = 0;
    int total_thread_pops = 0;
    
    for (int i = 0; i < NUM_THREADS; i++) {
        total_thread_pushes += args[i].pushes;
        total_thread_pops += args[i].pops;
    }
    
    // Count remaining nodes
    int remaining = 0;
    t_stack_top top = atomic_load(&stack->top);
	t_stack_node *current = top.node;
    while (current) {
        remaining++;
        current = current->next;
    }
    
    printf("\n=== Test Results ===\n");
    printf("Execution time: %.3f seconds\n", elapsed);
    printf("Total pushes (atomic): %d\n", total_pushes);
    printf("Total pushes (thread sum): %d\n", total_thread_pushes);
    printf("Total pops attempted: %d\n", total_pops);
    printf("Successful pops: %d\n", successful_pops);
    printf("Failed pops (empty stack): %d\n", failed_pops);
    printf("Remaining nodes in stack: %d\n", remaining);
    
    // Validation
    printf("\n=== Validation ===\n");
    if (total_thread_pushes == (successful_pops + remaining)) {
        printf("✓ PASS: Pushes (%d) == Pops (%d) + Remaining (%d)\n", 
               total_thread_pushes, successful_pops, remaining);
    } else {
        printf("✗ FAIL: Pushes (%d) != Pops + Remaining (%d + %d)\n", 
               total_thread_pushes, successful_pops, remaining);
    }
    
    // Drain and free all remaining nodes
    printf("\n=== Cleaning up ===\n");
    int freed_count = 0;
    t_stack_node *node;
    while ((node = pop(stack)) != NULL) {
        if (node->data) {
            freed_count++;
            free(node->data);
        }
        free(node);
    }
    printf("Freed %d remaining nodes\n", freed_count);
    
    // Cleanup
    free(stack);
    
    printf("\n=== Test Completed ===\n");
    printf("\nTo check for memory leaks:\n");
    printf("valgrind --leak-check=full ./atomic_stack\n");
    
    return 0;
}
