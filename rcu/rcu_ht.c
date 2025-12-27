#include "rcu_ht.h"
#include <stdlib.h>

static inline size_t knuth_hash(int key, size_t mask)
{
	return (size_t)(key * 2654435761) & mask;
}

// retrieves the entry from the wait-free queue rcu-head
static inline void free_callback(struct rcu_head *rcu_head)
{
	rcu_ht_entry_t	*entry = caa_container_of(rcu_head, rcu_ht_entry_t, rcu);
	free(entry->value);
	free(entry);
}
// use pow-of-2 size to use bitwise AND in hash --> much better performance
hashtable_t	*ht_create(size_t size)
{
	hashtable_t	*ht = malloc(sizeof(hashtable_t));
	if (!ht)
		return NULL;
	size_t actual_size = 1;
	while (actual_size < size)
		actual_size <<= 1;
	ht->buckets = calloc(actual_size, sizeof(struct cds_hlist_head));
	if (!ht->buckets)
		return (free(ht), NULL);
	for (size_t i = 0; i < actual_size; i++)
		CDS_INIT_HLIST_HEAD(&ht->buckets[i]);
	ht->bucket_locks = malloc(actual_size * sizeof(pthread_mutex_t));
	if (!ht->bucket_locks)
		return (free(ht->buckets), free(ht), NULL);
	for (size_t i = 0; i < actual_size; i++)
		pthread_mutex_init(&ht->bucket_locks[i], NULL);
	ht->size = actual_size;
	ht->mask = actual_size - 1;
	ht->hash_f = &knuth_hash;
	ht->free_c = &free_callback;
	return ht;
}

// inserts new_entry at the head of the bucket, updates if key exists
void	ht_insert(hashtable_t *ht, int key, void *value)
{
	size_t i = ht->hash_f(key, ht->mask);
	rcu_ht_entry_t *curr = NULL;

	pthread_mutex_lock(&ht->bucket_locks[i]);

	// update
	cds_hlist_for_each_entry_2(curr, &ht->buckets[i], node)
	{
		if (curr->key == key)
		{
			// remove old entry from table
			cds_hlist_del_rcu(&curr->node);
			//new entry
			rcu_ht_entry_t *new_entry = malloc(sizeof(rcu_ht_entry_t));
			if (!new_entry)
			{	//restore old entry
				cds_hlist_add_head_rcu(&curr->node, &ht->buckets[i]);
				pthread_mutex_unlock(&ht->bucket_locks[i]);
				free(value);
				return;
			}
			new_entry->key = key;
			new_entry->value = value;

			// insert new entry
			cds_hlist_add_head_rcu(&new_entry->node, &ht->buckets[i]);
			pthread_mutex_unlock(&ht->bucket_locks[i]);
			//deferred free of old entry
			call_rcu(&curr->rcu, free_callback);
			return;
		}
	}
	
	// insert: create entry then swing ptr
	rcu_ht_entry_t *new_entry = malloc(sizeof(rcu_ht_entry_t));
	if (!new_entry) return (pthread_mutex_unlock(&ht->bucket_locks[i]), free(value));
	new_entry->key = key;
	new_entry->value = value;
	cds_hlist_add_head_rcu(&new_entry->node, &ht->buckets[i]);

	pthread_mutex_unlock(&ht->bucket_locks[i]);
}

// traverses the bucket's list; null if not found
inline void	*ht_lookup(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->mask);
	rcu_ht_entry_t *entry = NULL;
	void	*result = NULL;

	rcu_read_lock();

	cds_hlist_for_each_entry_rcu_2(entry, &ht->buckets[i], node)
	{
		if (entry->key == key)
		{
			result = entry->value;
			break;
		}
	}

	rcu_read_unlock();
	return result;
}

void	ht_delete(hashtable_t *ht, int key)
{
	size_t i = ht->hash_f(key, ht->mask);
	rcu_ht_entry_t *curr = NULL;

	pthread_mutex_lock(&ht->bucket_locks[i]);
	cds_hlist_for_each_entry_2(curr, &ht->buckets[i], node)
	{
		if (curr->key == key)
		{
			cds_hlist_del_rcu(&curr->node);
			pthread_mutex_unlock(&ht->bucket_locks[i]);
			call_rcu(&curr->rcu, ht->free_c);
			return;
		}
	}
	pthread_mutex_unlock(&ht->bucket_locks[i]);
}

// standard for malloc'd values
void	ht_destroy(hashtable_t *ht)
{
	if (!ht) return;
	for (size_t i = 0; i < ht->size; i++)
	{
		pthread_mutex_lock(&ht->bucket_locks[i]);
		rcu_ht_entry_t *entry, *tmp;
		cds_hlist_for_each_entry_safe_2(entry, tmp, &ht->buckets[i], node)
		{
			cds_hlist_del(&entry->node);
			free(entry->value);
			free(entry);
		}
		pthread_mutex_unlock(&ht->bucket_locks[i]);
		pthread_mutex_destroy(&ht->bucket_locks[i]);
	}
	free(ht->buckets);
	free(ht->bucket_locks);
	free(ht);
}