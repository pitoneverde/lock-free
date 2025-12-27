#ifdef RWLOCK_HASHTABLE
#include "rw_ht.h"
#elif defined(RCU_HASHTABLE)
#include "rcu_ht.h"
#endif
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <time.h>

#define CONTENTION_KEY 42

void *reader_thread(void *arg) {
    hashtable_t *ht = (hashtable_t *)arg;
    for (int i = 0; i < 10000; i++) {
        // All readers try to read the same key
        void *val = ht_lookup(ht, CONTENTION_KEY);
        assert(val != NULL); // Should always find it
    }
    return NULL;
}

void *writer_thread(void *arg) {
    hashtable_t *ht = (hashtable_t *)arg;
    for (int i = 0; i < 100; i++) {
        // Writer updates the same key
        int *new_val = malloc(sizeof(int));
        *new_val = i;
        ht_insert(ht, CONTENTION_KEY, new_val);
        usleep(100); // Small delay to increase chance of interleaving
    }
    return NULL;
}

int test_rwlock_contention() {
    hashtable_t *ht = ht_create(1024);
    // First, insert the contended key
    int *initial_val = malloc(sizeof(int));
    *initial_val = -1;
    ht_insert(ht, CONTENTION_KEY, initial_val);
    
    pthread_t readers[4], writer;
    // Start 4 readers and 1 writer
    for (int i = 0; i < 4; i++) pthread_create(&readers[i], NULL, reader_thread, ht);
    pthread_create(&writer, NULL, writer_thread, ht);
    
    // Wait for all
    for (int i = 0; i < 4; i++) pthread_join(readers[i], NULL);
    pthread_join(writer, NULL);
    
    // Final sanity check
    assert(ht_lookup(ht, CONTENTION_KEY) != NULL);
    ht_destroy(ht);
    printf("PASS: RW Lock contention test\n");
	return 1;
}

typedef struct {
    hashtable_t *ht;
    long long *ops_done; // Shared counter for throughput calculation
    uint thread_id;
    int start_key;
    int key_range;    // Range of keys to operate on
    int read_percent; // e.g., 95 for 95% reads, 5% writes
    uint64_t duration_us;  // How long to run (e.g., 2 seconds = 2,000,000 µs)
} bench_args_t;

uint64_t get_monotonic_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

void *benchmark_worker(void *arg) {
    bench_args_t *args = (bench_args_t *)arg;
    long long local_ops = 0;
    uint64_t start_time = get_monotonic_time_us(); // You need a monotonic clock

    while ((get_monotonic_time_us() - start_time) < args->duration_us) {
        int key = (rand_r(&args->thread_id) % args->key_range);
        if ((rand_r(&args->thread_id) % 100) < args->read_percent) {
            // Read operation
            ht_lookup(args->ht, key);
        } else {
            // Write operation (update/insert)
            int *new_val = malloc(sizeof(int));
            *new_val = rand_r(&args->thread_id);
            ht_insert(args->ht, key, new_val);
        }
        local_ops++;
    }
    __sync_fetch_and_add(args->ops_done, local_ops); // Atomic add to shared counter
    return NULL;
}

void run_scaling_benchmark(int table_size, int key_range, int read_percent) {
    printf("\n=== Bench: %d%% Reads, %d Keys ===\n", read_percent, key_range);
    hashtable_t *ht = ht_create(table_size);

    // Pre-populate the table to avoid all inserts being misses
    for (int i = 0; i < key_range; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        ht_insert(ht, i, val);
    }

    const int TEST_DURATION_US = 2000000; // 2 seconds
    int thread_counts[] = {1, 2, 4}; // Test 1, 2, and 4 threads

    for (size_t t = 0; t < sizeof(thread_counts)/sizeof(thread_counts[0]); t++) {
        int num_threads = thread_counts[t];
        pthread_t threads[num_threads];
        bench_args_t args[num_threads];
        long long total_ops = 0;

        uint64_t start = get_monotonic_time_us();
        for (int i = 0; i < num_threads; i++) {
            args[i].ht = ht;
            args[i].ops_done = &total_ops;
            args[i].thread_id = i;
            args[i].key_range = key_range;
            args[i].read_percent = read_percent;
            args[i].duration_us = TEST_DURATION_US;
            pthread_create(&threads[i], NULL, benchmark_worker, &args[i]);
        }
        for (int i = 0; i < num_threads; i++) {
            pthread_join(threads[i], NULL);
        }
        uint64_t elapsed_us = get_monotonic_time_us() - start;

        double throughput = (double)total_ops / ((double)elapsed_us / 1000000.0);
        printf("Threads %d: %10.2f ops/sec\n", num_threads, throughput);
    }
    ht_destroy(ht);
}

int main()
{
#ifdef RCU_HASHTABLE
    rcu_register_thread();
#endif
#ifdef RWLOCK_HASHTABLE
	test_rwlock_contention();
#endif
	// Test in-cache (1K keys) and out-of-cache (100K keys) scenarios
    size_t key_counts[] = {1000, 100000};
    int read_ratios[] = {50, 90, 95, 99};

    for (int k = 0; k < 2; k++) {
        for (int r = 0; r < 4; r++) {
            run_scaling_benchmark(4096, key_counts[k], read_ratios[r]);
        }
    }
#ifdef RCU_HASHTABLE
    rcu_unregister_thread();
#endif
	return 0;
}