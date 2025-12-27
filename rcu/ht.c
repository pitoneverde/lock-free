#include "ht.h"
#include <stdlib.h>

static inline size_t knuth_hash(int key, size_t mask)
{
	return (size_t)(key * 2654435761) & mask;
}

// use pow-of-2 size to use bitwise AND in hash --> much better performance
hashtable_t	*ht_create(size_t size)
{
	hashtable_t	*ht = malloc(sizeof(hashtable_t));
	if (!ht) return NULL;
	size_t actual_size = 1;
	while (actual_size < size) actual_size <<= 1;
	ht->buckets = calloc(actual_size, sizeof(ht_entry_t *));
	if (!ht->buckets) return (free(ht), NULL);
	ht->size = actual_size;
	ht->mask = actual_size - 1;
	ht->hash_f = &knuth_hash;
	return ht;
}

// inserts new_entry at the head of the bucket, updates if key exists
void	ht_insert(hashtable_t *ht, int key, void *value)
{
	size_t i = ht->hash_f(key, ht->mask);

	// update
	ht_entry_t *curr = ht->buckets[i];
	while (curr)
	{
		if (curr->key == key)
		{
			free(curr->value);
			curr->value = value;
			return;
		}
		curr = curr->next;
	}
	
	// insert
	ht_entry_t *new_entry = malloc(sizeof(ht_entry_t));
	if (!new_entry) return free(value);
	new_entry->key = key;
	new_entry->value = value;
	new_entry->next = ht->buckets[i];
	ht->buckets[i] = new_entry;
}

// traverses the bucket's list; null if not found
inline void	*ht_lookup(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->mask);
	ht_entry_t *entry = ht->buckets[i];

	if (entry && entry->next)
		__builtin_prefetch(entry->next, 0, 1);
	
	while (entry) {
		if (entry->key == key)
			return entry->value;
		entry = entry->next;
	}
	return NULL;
}

void	ht_delete(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->mask);
	ht_entry_t *curr = ht->buckets[i];
	ht_entry_t *prev = NULL;
	while (curr)
	{
		if (curr->key == key)
		{
			if (prev == NULL)
				ht->buckets[i] = curr->next;
			else
				prev->next = curr->next;
			free(curr->value);
			free(curr);
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

// standard for malloc'd values
void	ht_destroy(hashtable_t *ht)
{
	if (!ht) return;
	for (size_t i = 0; i < ht->size; i++)
	{
		ht_entry_t *entry = ht->buckets[i];
		while (entry)
		{
			ht_entry_t *next = entry->next;
			free(entry->value);
			free(entry);
			entry = next;
		}
	}
	free(ht->buckets);
	free(ht);
}