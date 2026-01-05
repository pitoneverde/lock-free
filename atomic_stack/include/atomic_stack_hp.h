// atomic stack with tagged pointers

#define _POSIX_C_SOURCE 200809L // For clock_gettime

#ifndef ATOMIC_STACK_HP_H
#define ATOMIC_STACK_HP_H
#include <stdatomic.h>
#include <unistd.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>

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

//========== Hazard Pointers API ===========

#define MAX_THREADS 64
#define HP_PER_THREAD 2			//current & next
#define RETIRE_CAPACITY 100
#define SCAN_THRESHOLD 50

typedef struct {
    _Atomic(uintptr_t) ptr;
} hp_slot_t;

// Per-thread data
typedef struct hp_thread {
    hp_slot_t slots[HP_PER_THREAD];
    
    // Retire array (dynamic, grows as needed)
    void **retire_list;
    size_t retire_size;
	size_t retire_capacity;
} hp_thread_t;

// Global registry
static hp_thread_t *hp_registry[MAX_THREADS];
static _Atomic size_t hp_next_index = 0;

// Fast path
static _Thread_local hp_thread_t *tl_hp = NULL;

void hp_init_thread(void);
void hp_cleanup_thread(void);
void hp_protect(int slot, void *ptr);
void hp_clear(int slot);
void hp_retire(void *ptr);

#endif