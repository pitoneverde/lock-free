#include "rw_ht.h"
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
	ht->size = 0;		// to use ht_destroy safely
	ht->bucket_locks = malloc(actual_size * sizeof(pthread_rwlock_t));
	if (!ht->bucket_locks) return (ht_destroy(ht), NULL);
	for (size_t i = 0; i < actual_size; i++)
		pthread_rwlock_init(&ht->bucket_locks[i], NULL);
	ht->size = actual_size;
	ht->mask = actual_size - 1;
	ht->hash_f = &knuth_hash;
	return ht;
}

// inserts new_entry at the head of the bucket, updates if key exists
void	ht_insert(hashtable_t *ht, int key, void *value)
{
	size_t i = ht->hash_f(key, ht->mask);

	pthread_rwlock_wrlock(&ht->bucket_locks[i]);
	
	// update
	ht_entry_t *curr = ht->buckets[i];
	while (curr)
	{
		if (curr->key == key)
		{
			free(curr->value);
			curr->value = value;
			pthread_rwlock_unlock(&ht->bucket_locks[i]);
			return;
		}
		curr = curr->next;
	}
	
	// insert
	ht_entry_t *new_entry = malloc(sizeof(ht_entry_t));
	if (!new_entry)
	{
		pthread_rwlock_unlock(&ht->bucket_locks[i]);
		return free(value);
	}
	new_entry->key = key;
	new_entry->value = value;
	new_entry->next = ht->buckets[i];
	ht->buckets[i] = new_entry;
	
	pthread_rwlock_unlock(&ht->bucket_locks[i]);
}

// traverses the bucket's list; null if not found
inline void	*ht_lookup(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->mask);
	void	*result = NULL;

	pthread_rwlock_rdlock(&ht->bucket_locks[i]);
	ht_entry_t *entry = ht->buckets[i];

	if (entry && entry->next)
		__builtin_prefetch(entry->next, 0, 1);
	
	while (entry) {
		if (entry->key == key)
			result = entry->value;
		entry = entry->next;
	}
	pthread_rwlock_unlock(&ht->bucket_locks[i]);

	return result;
}

void	ht_delete(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->mask);

	pthread_rwlock_wrlock(&ht->bucket_locks[i]);

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
			pthread_rwlock_unlock(&ht->bucket_locks[i]);
			return;
		}
		prev = curr;
		curr = curr->next;
	}

	pthread_rwlock_unlock(&ht->bucket_locks[i]);
}

// frees locks before data
void	ht_destroy(hashtable_t *ht)
{
	if (!ht) return;
	
	for (size_t i = 0; i < ht->size; i++)
		pthread_rwlock_destroy(&ht->bucket_locks[i]);
	free(ht->bucket_locks);	

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