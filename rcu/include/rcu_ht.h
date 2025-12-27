#ifndef RCU_HT_H
# define RCU_HT_H
# define _GNU_SOURCE
# include <unistd.h>
# include <pthread.h>
# include <urcu.h>
# include <urcu/rcuhlist.h>

// rcu-protected linked-list
typedef struct rcu_ht_entry
{
	int		key;
	void	*value;
	struct cds_hlist_node node;		// rcu list node instead of *next
	struct rcu_head	rcu;			// for deferred free
} rcu_ht_entry_t;

typedef size_t (*hash_function)(int key, size_t size);
typedef void (*free_function)(struct rcu_head *rcu_head);

// rcu
typedef struct hashtable_s
{
	struct cds_hlist_head	*buckets;
	pthread_mutex_t	*bucket_locks;		// for writers
	size_t	size;
	size_t	mask;
	hash_function hash_f;
	free_function free_c;
} hashtable_t;

hashtable_t	*ht_create(size_t size);
void	ht_destroy(hashtable_t *ht);

void	ht_insert(hashtable_t *ht, int key, void *value);
void	*ht_lookup(hashtable_t *ht, int key);
void	ht_delete(hashtable_t *ht, int key);

#endif