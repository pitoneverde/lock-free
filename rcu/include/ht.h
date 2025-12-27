#ifndef HT_H
# define HT_H
# define _GNU_SOURCE
# include <unistd.h>

// Linked-list
typedef struct ht_entry_s
{
	int		key;
	void	*value;
	struct ht_entry_s	*next;
} ht_entry_t;

typedef size_t (*hash_function)(int key, size_t size);

// base
typedef struct hashtable_s
{
	ht_entry_t	**buckets;
	size_t	size;
	size_t	mask;
	hash_function hash_f;
} hashtable_t;

hashtable_t	*ht_create(size_t size);
void	ht_destroy(hashtable_t *ht);
void	ht_insert(hashtable_t *ht, int key, void *value);
void	*ht_lookup(hashtable_t *ht, int key);
void	ht_delete(hashtable_t *ht, int key);

#endif