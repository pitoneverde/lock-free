#include "atomic_stack_hp.h"

static void hp_scan_and_reclaim(void);

// Allocates per-thread data and adds it to registry
void hp_init_thread(void)
{
	if (tl_hp) return;
	hp_thread_t *hp = malloc(sizeof(hp_thread_t));
	if (!hp) abort();
	for (size_t i = 0; i < HP_PER_THREAD; i++)
		atomic_init(&hp->slots[i].ptr, 0);
	hp->retire_capacity = RETIRE_CAPACITY;
	hp->retire_size = 0;
	hp->retire_list = malloc(hp->retire_capacity * sizeof(void *));
	if (!hp->retire_list)
	{
		free(hp);
		abort();
	}	
	size_t i = atomic_fetch_add(&hp_next_index, 1);
	if (i < MAX_THREADS)
	{
		hp_registry[i] = hp;
		tl_hp = hp;
	}
	else
	{
		free(hp->retire_list);
		free(hp);
		abort();
	}
}

// frees per-thread data and removes it from registry
void hp_cleanup_thread(void)
{
	if (!tl_hp) return;
	for (size_t i = 0; i < tl_hp->retire_size; i++)
	{
		free(((t_stack_node *)tl_hp->retire_list[i])->data);
		free(tl_hp->retire_list[i]);
	}
	free(tl_hp->retire_list);
	
	for (size_t i = 0; i < MAX_THREADS; i++)
		if (hp_registry[i] == tl_hp)
		{
			hp_registry[i] = NULL;
			break;
		}
	
	free(tl_hp);
	tl_hp = NULL;
}

// Must immediatly be visible
void hp_protect(int slot, void *ptr)
{
	if (slot < 0 || slot >= HP_PER_THREAD) return;
	atomic_store(&tl_hp->slots[slot].ptr, (uintptr_t)ptr);
}

// No need for sync
void hp_clear(int slot)
{
	if (slot < 0 || slot >= HP_PER_THREAD) return;
	atomic_store_explicit(&tl_hp->slots[slot].ptr, 0, memory_order_relaxed);
}

void hp_retire(void *ptr)
{
	if (!tl_hp) hp_init_thread();
	// resize array if needed
	if (tl_hp->retire_size >= tl_hp->retire_capacity)
	{
		hp_scan_and_reclaim();
		if (tl_hp->retire_size >= tl_hp->retire_capacity)
		{
			size_t new_cap = tl_hp->retire_capacity * 2;
			void **new_list = realloc(tl_hp->retire_list, new_cap * sizeof(void *));
			if (new_list)
			{
				tl_hp->retire_list = new_list;
				tl_hp->retire_capacity = new_cap;
			}
			else hp_scan_and_reclaim();
		}
	}

	tl_hp->retire_list[tl_hp->retire_size++] = ptr;
	if (tl_hp->retire_size > SCAN_THRESHOLD)
		hp_scan_and_reclaim();
}

static void hp_scan_and_reclaim(void)
{
	if (!tl_hp) return;
	// Collect all HP from all threads
	uintptr_t protected[HP_PER_THREAD * MAX_THREADS];
	size_t n = 0;
	for(size_t i = 0; i < MAX_THREADS; i++)
	{
		hp_thread_t *hp = hp_registry[i];
		if (!hp) continue;
		for (size_t j = 0; j < HP_PER_THREAD; j++)
		{
			uintptr_t ptr = atomic_load_explicit(&hp->slots[j].ptr, memory_order_acquire);
			if (ptr)
				protected[n++] = ptr;
		}
	}

	// Filter for retire list
	size_t new_n = 0;
	for (size_t i = 0; i < tl_hp->retire_size; i++)
	{
		void *node = tl_hp->retire_list[i];
		int is_protected = 0;
		for (size_t j = 0; j < n; j++)
		{
			if (protected[j] == (uintptr_t)node)
			{
				is_protected = 1;
				break;
			}
		}
		if (is_protected)
			tl_hp->retire_list[new_n++] = node;
		else
		{
			free(((t_stack_node *)node)->data);
			free(node);
		}
	}
	tl_hp->retire_size = new_n;
}
