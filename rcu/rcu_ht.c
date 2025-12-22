#include "rcu_ht.h"

static size_t knuth_hash(int key, size_t table_size)
{
	return (size_t)(key * 2654435761) % table_size;
}

hashtable_t	*ht_create(size_t size)
{
	hashtable_t	*ht = malloc(sizeof(hashtable_t));
	if (!ht) return NULL;
	ht->buckets = calloc(size, sizeof(ht_entry_t *));
	if (!ht->buckets) return (free(ht), NULL);
	ht->size = size;
	ht->hash_f = &knuth_hash;
}

// inserts new_entry at the head of the bucket
void	ht_insert(hashtable_t *ht, int key, void *value)
{
	size_t i = ht->hash_f(key, ht->size);
	ht_entry_t *new_entry = malloc(sizeof(ht_entry_t));
	if (!new_entry) return NULL;
	new_entry->key = key;
	new_entry->value = value;
	new_entry->next = ht->buckets[i];
	ht->buckets[i] = new_entry;
}

// traverses the bucket's list; null if not found
void	*ht_lookup(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->size);
	ht_entry_t *entry = ht->buckets[i];
	while (entry) {
		if (entry->key == key)
			return entry->value;
		entry = entry->next;
	}
	return NULL;
}

void	ht_delete(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->size);
	ht_entry_t *entry = ht->buckets[i];
	while (entry)
	{
		if (entry->key == key)
		{
			ht->buckets[i] = entry->next;
			entry->next = NULL;
			free(entry);
		}
		entry = entry->next;
	}
}