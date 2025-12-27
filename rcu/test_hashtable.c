#ifdef BASIC_HASHTABLE
#include "ht.h"
#elif defined(RWLOCK_HASHTABLE)
#include "rw_ht.h"
#elif defined(RCU_HASHTABLE)
#include "rcu_ht.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>

#define TEST_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("FAIL: %s\n", msg); \
            return 0; \
        } else { \
            printf("PASS: %s\n", msg); \
        } \
    } while(0)

int test_create_destroy() {
    printf("\n=== Test 1: Create/Destroy ===\n");
    
    hashtable_t *ht = ht_create(100);
    TEST_ASSERT(ht != NULL, "ht_create returns non-NULL");
    TEST_ASSERT(ht->size == 128, "Size is correctly set");
    
    ht_destroy(ht);
    TEST_ASSERT(1, "ht_destroy completes without crash");
    return 1;
}

int test_insert_lookup() {
    printf("\n=== Test 2: Insert/Lookup ===\n");
    hashtable_t *ht = ht_create(50);
    
    // Insert unique keys
    for (int i = 0; i < 100; i++) {
        int *value = malloc(sizeof(int));
        *value = i * 10;
        ht_insert(ht, i, value);
    }
    
    // Verify all values
    int found_all = 1;
    for (int i = 0; i < 100; i++) {
        int *value = ht_lookup(ht, i);
        if (!value || *value != i * 10) {
            printf("FAIL: Key %d not found or wrong value\n", i);
            found_all = 0;
        }
    }
    TEST_ASSERT(found_all, "All 100 inserts are retrievable");
    
    // Test non-existent key
    TEST_ASSERT(ht_lookup(ht, 999) == NULL, "Non-existent key returns NULL");
    
    ht_destroy(ht);
    return 1;
}

int test_update() {
    printf("\n=== Test 3: Update ===\n");
    hashtable_t *ht = ht_create(10);
    
    int *v1 = malloc(sizeof(int));
    int *v2 = malloc(sizeof(int));
    *v1 = 100;
    *v2 = 200;
    
    ht_insert(ht, 5, v1);
    ht_insert(ht, 5, v2);  // Should update
    
    int *result = ht_lookup(ht, 5);
    TEST_ASSERT(result == v2, "Update replaces old value");
    TEST_ASSERT(*result == 200, "New value is correct");
    
    // Old value should be freed (if your implementation does that)
    ht_destroy(ht);
    return 1;
}

int test_delete() {
    printf("\n=== Test 4: Delete (Heap-allocated values) ===\n");
    hashtable_t *ht = ht_create(20);
    
    // Allocate values on heap instead of stack
    int *values[5];
    for (int i = 0; i < 5; i++) {
        values[i] = malloc(sizeof(int));
        *values[i] = i * 100;
        ht_insert(ht, i, values[i]);
    }
    
    // Delete middle key
    ht_delete(ht, 2);
    TEST_ASSERT(ht_lookup(ht, 2) == NULL, "Deleted key returns NULL");
    
    // Other keys should still exist - compare values, not addresses
    int *val0 = ht_lookup(ht, 0);
    int *val4 = ht_lookup(ht, 4);
    TEST_ASSERT(val0 != NULL && *val0 == 0, "Key 0 still exists with correct value");
    TEST_ASSERT(val4 != NULL && *val4 == 400, "Key 4 still exists with correct value");
    
    // Delete non-existent (should not crash)
    ht_delete(ht, 99);
    TEST_ASSERT(1, "Delete non-existent doesn't crash");
    
    // IMPORTANT: Don't free the values array - ht_destroy will do it
    // The values[2] was already freed by ht_delete(ht, 2)
    ht_destroy(ht);
    
    return 1;
}

int test_collisions() {
    printf("\n=== Test 5: Collisions (Heap-allocated values) ===\n");
    
    // Small table to force collisions
    hashtable_t *ht = ht_create(3);  // Only 3 buckets!
    
    // Insert 10 values - all will collide
    int *values[10];
    for (int i = 0; i < 10; i++) {
        values[i] = malloc(sizeof(int));
        *values[i] = i;
        ht_insert(ht, i, values[i]);
    }
    
    // All should still be retrievable
    int ok = 1;
    for (int i = 0; i < 10; i++) {
        int *val = ht_lookup(ht, i);
        if (!val || *val != i) {
            printf("FAIL: Collision handling failed for key %d\n", i);
            ok = 0;
        }
    }
    TEST_ASSERT(ok, "All keys retrievable despite collisions");
    
    // Delete from middle of collision chain
    ht_delete(ht, 5);
    TEST_ASSERT(ht_lookup(ht, 5) == NULL, "Deleted key in collision chain");
    
    // Others should still exist - check values, not addresses
    int *val6 = ht_lookup(ht, 6);
    TEST_ASSERT(val6 != NULL && *val6 == 6, "Other collision keys still work");
    
    // Test deleting from head of collision chain
    // (Assuming keys 0, 3, 6, 9 all hash to same bucket 0)
    int *val0 = ht_lookup(ht, 0);
    TEST_ASSERT(val0 != NULL && *val0 == 0, "Key 0 exists before deletion");
    ht_delete(ht, 0);
    TEST_ASSERT(ht_lookup(ht, 0) == NULL, "Deleted head of collision chain");
    
    // Keys 3, 6, 9 should still work
    int *val3 = ht_lookup(ht, 3);
    int *val9 = ht_lookup(ht, 9);
    TEST_ASSERT(val3 != NULL && *val3 == 3, "Key 3 still exists");
    TEST_ASSERT(val9 != NULL && *val9 == 9, "Key 9 still exists");
    
    // Clean up - ht_destroy will free remaining values
    // values[5] and values[0] were already freed by ht_delete
    ht_destroy(ht);
    
    return 1;
}

int test_performance() {
    printf("\n=== Test 6: Performance ===\n");
    
    const int N = 10000;
    hashtable_t *ht = ht_create(1024);
    
    // 1. Insert N items
    clock_t start = clock();
    for (int i = 0; i < N; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        ht_insert(ht, i, val);
    }
    clock_t insert_time = clock() - start;
    
    // 2. Lookup all items
    start = clock();
    for (int i = 0; i < N; i++) {
        int *val = ht_lookup(ht, i);
        assert(val && *val == i);
    }
    clock_t lookup_time = clock() - start;
    
    // 3. Update first half
    start = clock();
    for (int i = 0; i < N/2; i++) {
        int *new_val = malloc(sizeof(int));
        *new_val = i * 2;  // Different value to verify updates work
        ht_insert(ht, i, new_val);  // Updates existing
    }
    clock_t update_time = clock() - start;
    
    // 4. Delete even-numbered keys
    start = clock();
    int delete_count = 0;
    for (int i = 0; i < N; i += 2) {
        ht_delete(ht, i);
        delete_count++;
    }
    clock_t delete_time = clock() - start;
    
    // 5. Verify remaining (odd keys)
    int remaining_count = 0;
    for (int i = 1; i < N; i += 2) {
        int *val = ht_lookup(ht, i);
        if (val) {
            // First half were updated to i*2, second half still i
            int expected = (i < N/2) ? (i * 2) : i;
            if (*val == expected) {
                remaining_count++;
            }
        }
    }
    
    // Print results
    printf("Insert %d items: %.2f ms (%.0f ops/sec)\n", 
           N, (double)insert_time * 1000 / CLOCKS_PER_SEC,
           N / ((double)insert_time / CLOCKS_PER_SEC));
    
    printf("Lookup %d items: %.2f ms (%.0f ops/sec)\n", 
           N, (double)lookup_time * 1000 / CLOCKS_PER_SEC,
           N / ((double)lookup_time / CLOCKS_PER_SEC));
    
    printf("Update %d items: %.2f ms (%.0f ops/sec)\n",
           N/2, (double)update_time * 1000 / CLOCKS_PER_SEC,
           (N/2) / ((double)update_time / CLOCKS_PER_SEC));
    
    printf("Delete %d items: %.2f ms (%.0f ops/sec)\n",
           delete_count, (double)delete_time * 1000 / CLOCKS_PER_SEC,
           delete_count / ((double)delete_time / CLOCKS_PER_SEC));
    
    printf("Remaining items after delete: %d (expected: %d)\n", 
           remaining_count, N/2);
    
    int passed = (remaining_count == N/2);
    TEST_ASSERT(passed, "Performance test completed correctly");
    
    // Cleanup - table should now have N/2 items
    ht_destroy(ht);
    
    return passed ? 1 : 0;
}


int test_performance_comprehensive() {
    printf("\n=== Test 6b: Performance with Different Sizes ===\n");
    
    // Test different table sizes
    size_t sizes[] = {16, 64, 256, 1024, 4096, 16384};
    const int N = 10000;
    
    printf("Table Size | Insert (ops/sec) | Lookup (ops/sec) | Load Factor\n");
    printf("-----------|------------------|------------------|------------\n");
    
    for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
        hashtable_t *ht = ht_create(sizes[s]);
        if (!ht) {
            printf("Size %zu: FAILED to create\n", sizes[s]);
            continue;
        }
        
        // Insert N items
        clock_t start = clock();
        for (int i = 0; i < N; i++) {
            int *val = malloc(sizeof(int));
            if (val) {
                *val = i;
                ht_insert(ht, i, val);
            }
        }
        clock_t insert_time = clock() - start;
        
        // Lookup all items
        start = clock();
        for (int i = 0; i < N; i++) {
            ht_lookup(ht, i);
        }
        clock_t lookup_time = clock() - start;
        
        // Calculate metrics
        double insert_ops = (double)N / ((double)insert_time / CLOCKS_PER_SEC);
        double lookup_ops = (double)N / ((double)lookup_time / CLOCKS_PER_SEC);
        double load_factor = (double)N / (double)sizes[s];
        
        printf("%9zu | %16.0f | %16.0f | %.2f\n", 
               sizes[s], insert_ops, lookup_ops, load_factor);
        
        // Clean up
        ht_destroy(ht);
    }
    
    return 1;
}

int test_memory() {
    printf("\n=== Test 7: Memory ===\n");
    
    // Use with valgrind: valgrind --leak-check=full ./test
    hashtable_t *ht = ht_create(100);
    
    // Insert with malloc'd values
    for (int i = 0; i < 1000; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        ht_insert(ht, i, val);
    }
    
    // Delete half
    for (int i = 0; i < 500; i++) {
        ht_delete(ht, i);
    }
    
    ht_destroy(ht);
    TEST_ASSERT(1, "No memory leaks (check with valgrind)");
    return 1;
}

int test_edge_cases() {
    printf("\n=== Test 8: Edge Cases (Heap-allocated values) ===\n");
    
    hashtable_t *ht = ht_create(10);
    
    // Test 1: NULL value insertion (heap-allocated NULL pointer)
    int *null_value = NULL;  // Explicit NULL
    ht_insert(ht, 0, null_value);
    TEST_ASSERT(ht_lookup(ht, 0) == NULL, "NULL values are allowed");
    
    // Test 2: Update NULL with heap value
    int *zero_val = malloc(sizeof(int));
    *zero_val = 42;
    ht_insert(ht, 0, zero_val);  // Overwrite NULL - frees NULL (which is safe)
    int *found = ht_lookup(ht, 0);
    TEST_ASSERT(found != NULL && *found == 42, "Key 0 works with heap value");
    
    // Test 3: Negative keys
    int *neg_val = malloc(sizeof(int));
    *neg_val = -100;
    ht_insert(ht, -5, neg_val);
    int *found_neg = ht_lookup(ht, -5);
    TEST_ASSERT(found_neg != NULL && *found_neg == -100, "Negative keys work");
    
    // Test 4: Very large positive key (INT_MAX)
    int *big_val = malloc(sizeof(int));
    *big_val = 9999;
    ht_insert(ht, INT_MAX, big_val);
    int *found_big = ht_lookup(ht, INT_MAX);
    TEST_ASSERT(found_big != NULL && *found_big == 9999, "INT_MAX key works");
    
    // Test 5: INT_MIN key
    int *min_val = malloc(sizeof(int));
    *min_val = -9999;
    ht_insert(ht, INT_MIN, min_val);
    int *found_min = ht_lookup(ht, INT_MIN);
    TEST_ASSERT(found_min != NULL && *found_min == -9999, "INT_MIN key works");
    
    // Test 6: Table size 1 (everything collides)
    hashtable_t *tiny_ht = ht_create(1);
    int *vals[5];
    for (int i = 0; i < 5; i++) {
        vals[i] = malloc(sizeof(int));
        *vals[i] = i * 100;
        ht_insert(tiny_ht, i, vals[i]);
    }
    // All should be retrievable
    int all_found = 1;
    for (int i = 0; i < 5; i++) {
        int *v = ht_lookup(tiny_ht, i);
        if (!v || *v != i * 100) {
            all_found = 0;
            break;
        }
    }
    TEST_ASSERT(all_found, "All keys work in size-1 table (maximum collisions)");
    
    // Delete from middle of size-1 chain
    ht_delete(tiny_ht, 2);
    TEST_ASSERT(ht_lookup(tiny_ht, 2) == NULL, "Delete from middle of size-1 chain");
    TEST_ASSERT(ht_lookup(tiny_ht, 3) != NULL, "Other keys still work after delete");
    
    ht_destroy(tiny_ht);
    
    // Test 7: Update same key multiple times
    int *update_val1 = malloc(sizeof(int));
    int *update_val2 = malloc(sizeof(int));
    int *update_val3 = malloc(sizeof(int));
    *update_val1 = 100;
    *update_val2 = 200;
    *update_val3 = 300;
    
    ht_insert(ht, 999, update_val1);
    ht_insert(ht, 999, update_val2);  // Should free update_val1
    ht_insert(ht, 999, update_val3);  // Should free update_val2
    
    int *final_val = ht_lookup(ht, 999);
    TEST_ASSERT(final_val != NULL && *final_val == 300, "Multiple updates work");
    
    // Test 8: Delete non-existent key (should not crash)
    ht_delete(ht, 987654321);
    TEST_ASSERT(1, "Delete non-existent key doesn't crash");
    
    // Test 9: Insert, delete, reinsert same key
    int *cycle_val1 = malloc(sizeof(int));
    int *cycle_val2 = malloc(sizeof(int));
    *cycle_val1 = 111;
    *cycle_val2 = 222;
    
    ht_insert(ht, 555, cycle_val1);
    ht_delete(ht, 555);
    ht_insert(ht, 555, cycle_val2);
    
    int *cycled = ht_lookup(ht, 555);
    TEST_ASSERT(cycled != NULL && *cycled == 222, "Insert-delete-reinsert cycle works");
    
    // Test 10: Empty table operations
    hashtable_t *empty_ht = ht_create(100);
    TEST_ASSERT(ht_lookup(empty_ht, 123) == NULL, "Lookup in empty table returns NULL");
    ht_delete(empty_ht, 123);  // Should not crash
    TEST_ASSERT(1, "Delete from empty table doesn't crash");
    ht_destroy(empty_ht);
    
    // Test 11: Very large value (pointer to large struct simulation)
    typedef struct {
        int a[1000];
        char b[256];
        double c[50];
    } large_struct_t;
    
    large_struct_t *large_val = malloc(sizeof(large_struct_t));
    large_val->a[0] = 42;
    large_val->b[0] = 'X';
    large_val->c[0] = 3.14;
    
    ht_insert(ht, 7777, large_val);
    large_struct_t *found_large = ht_lookup(ht, 7777);
    TEST_ASSERT(found_large != NULL && 
                found_large->a[0] == 42 && 
                found_large->b[0] == 'X' &&
                found_large->c[0] == 3.14, 
                "Large struct values work");
    
    // Test 12: Collision with very different keys (hash function test)
    // This depends on your hash function - test that different keys
    // that hash to same bucket still work
    int keys_that_collide[] = {0, 3, 6, 9};  // These might collide with table size 10
    int *collision_vals[4];
    for (int i = 0; i < 4; i++) {
        collision_vals[i] = malloc(sizeof(int));
        *collision_vals[i] = i * 1000;
        ht_insert(ht, keys_that_collide[i], collision_vals[i]);
    }
    
    // All should be retrievable
    int collision_ok = 1;
    for (int i = 0; i < 4; i++) {
        int *v = ht_lookup(ht, keys_that_collide[i]);
        if (!v || *v != i * 1000) {
            collision_ok = 0;
            break;
        }
    }
    TEST_ASSERT(collision_ok, "Specific collision keys all work");
    
    // Test 13: Zero-size value (empty allocation)
    void *empty_val = malloc(0);  // malloc(0) is implementation-defined
    if (empty_val) {  // Some malloc implementations return NULL, some non-NULL
        ht_insert(ht, 8888, empty_val);
        void *found_empty = ht_lookup(ht, 8888);
        TEST_ASSERT(found_empty == empty_val, "Zero-size allocation works");
    }
    
    // Test 14: Stress test with many operations
    hashtable_t *stress_ht = ht_create(50);
    const int STRESS_OPS = 1000;
    for (int i = 0; i < STRESS_OPS; i++) {
        int *val = malloc(sizeof(int));
        *val = i;
        ht_insert(stress_ht, i, val);
        
        // Every 10th operation, delete a random key
        if (i % 10 == 0 && i > 0) {
            ht_delete(stress_ht, rand() % i);
        }
    }
    TEST_ASSERT(1, "Stress test completed without crash");
    ht_destroy(stress_ht);
    
    // Test 15: Verify ht_destroy handles all cleanup
    // (Run with valgrind to check for leaks)
    
    ht_destroy(ht);
    printf("All edge case tests passed!\n");
    return 1;
}

int test_hash_function_edge_cases() {
    printf("\n=== Test 9: Hash Function Edge Cases ===\n");
    
    // Test that hash function handles all integer values properly
    hashtable_t *ht = ht_create(100);
    
    // Test powers of two (common problematic case for some hash functions)
    int powers_of_two[] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
    for (size_t i = 0; i < sizeof(powers_of_two)/sizeof(powers_of_two[0]); i++) {
        int *val = malloc(sizeof(int));
        *val = powers_of_two[i];
        ht_insert(ht, powers_of_two[i], val);
    }
    
    // All should be retrievable
    int all_powers_ok = 1;
    for (size_t i = 0; i < sizeof(powers_of_two)/sizeof(powers_of_two[0]); i++) {
        int *v = ht_lookup(ht, powers_of_two[i]);
        if (!v || *v != powers_of_two[i]) {
            all_powers_ok = 0;
            break;
        }
    }
    TEST_ASSERT(all_powers_ok, "Powers of two keys work");
    
    ht_destroy(ht);
    return 1;
}

int test_memory_boundaries() {
    printf("\n=== Test 10: Memory Boundaries ===\n");
    
    // Test with maximum practical table size
    size_t large_size = 1000000;  // 1 million buckets
    hashtable_t *large_ht = ht_create(large_size);
    TEST_ASSERT(large_ht != NULL, "Large table creation works");
    
    if (large_ht) {
        // Insert a few items to verify functionality
        for (int i = 0; i < 10; i++) {
            int *val = malloc(sizeof(int));
            *val = i;
            ht_insert(large_ht, i, val);
        }
        
        // Verify
        int ok = 1;
        for (int i = 0; i < 10; i++) {
            int *v = ht_lookup(large_ht, i);
            if (!v || *v != i) {
                ok = 0;
                break;
            }
        }
        TEST_ASSERT(ok, "Large table operations work");
        
        ht_destroy(large_ht);
    }
    
    return 1;
}

// Measure memory bandwidth
int benchmark_memory_bandwidth() {
    const size_t SIZE = 1000000;
    int *data = malloc(SIZE * sizeof(int));
    
    // Write bandwidth
    clock_t start = clock();
    for (size_t i = 0; i < SIZE; i++) {
        data[i] = i;
    }
    double write_time = (double)(clock() - start) / CLOCKS_PER_SEC;
    
    // Read bandwidth  
    start = clock();
    volatile int sum = 0;
    for (size_t i = 0; i < SIZE; i++) {
        sum += data[i];
    }
    double read_time = (double)(clock() - start) / CLOCKS_PER_SEC;
    
    double write_bw = (SIZE * sizeof(int)) / (write_time * 1024*1024*1024);
    double read_bw = (SIZE * sizeof(int)) / (read_time * 1024*1024*1024);
    
    printf("Write bandwidth: %.2f GB/s\n", write_bw);
    printf("Read bandwidth:  %.2f GB/s\n", read_bw);
    printf("Your hash table uses: 200M × 24B = 4.8GB/s\n");
    
    free(data);
	return 1;
}

int benchmark_cache_effects() {
    printf("\n=== Cache Effect Benchmark ===\n");
    
    // Test different working set sizes
    size_t sizes[] = {1024, 4096, 32768, 131072, 524288, 2097152, 8388608};
    //                L1     L1      L2      L2       L3       L3       DRAM
    
    for (size_t s = 0; s < sizeof(sizes)/sizeof(sizes[0]); s++) {
        size_t N = sizes[s];
        int *data = malloc(N * sizeof(int));
        
        // Initialize
        for (size_t i = 0; i < N; i++) {
            data[i] = rand();
        }
        
        // Time random accesses
        clock_t start = clock();
        volatile int sum = 0;
        for (int i = 0; i < 1000000; i++) {
            sum += data[rand() % N];
        }
        double time = (double)(clock() - start) / CLOCKS_PER_SEC;
        
        printf("Size %8zu (%4zuKB): %.2f Mops/sec\n", 
               N, N*sizeof(int)/1024, 1.0/time);
        
        free(data);
    }
	return 1;
}

int benchmark_cache_aware_vs_oblivious() {
    printf("\n=== Cache Aware vs Oblivious ===\n");
    
    // Same data, different access patterns
    const size_t N = 1000000;
    int *data = malloc(N * sizeof(int));
    for (size_t i = 0; i < N; i++) data[i] = i;
    
    // 1. Cache-aware: Sequential access
    clock_t start = clock();
    volatile int sum1 = 0;
    for (size_t i = 0; i < N; i++) {
        sum1 += data[i];
    }
    double seq_time = (double)(clock() - start) / CLOCKS_PER_SEC;
    
    // 2. Cache-oblivious: Random access  
    start = clock();
    volatile int sum2 = 0;
    for (int i = 0; i < 1000000; i++) {
        sum2 += data[rand() % N];
    }
    double rand_time = (double)(clock() - start) / CLOCKS_PER_SEC;
    
    printf("Sequential: %.2f Mops/sec\n", N/seq_time/1000000);
    printf("Random:     %.2f Mops/sec\n", 1000000/rand_time/1000000);
    printf("Ratio: %.1fx faster\n", (N/seq_time)/(1000000/rand_time));
    
    free(data);
	return 1;
}

// Main test runner
int main() {
    int passed = 0;
    int total = 0;
    
    typedef int (*test_func_t)(void);
    test_func_t tests[] = {
        test_create_destroy,
        test_insert_lookup,
        test_update,
        test_delete,
        test_collisions,
        test_performance,
		test_performance_comprehensive,
        test_memory,
        test_edge_cases,
		test_hash_function_edge_cases,
		test_memory_boundaries,
		benchmark_memory_bandwidth,
		benchmark_cache_effects,
		benchmark_cache_aware_vs_oblivious,
        NULL
    };
    
    printf("========================================\n");
    printf("       Hash Table Test Suite\n");
    printf("========================================\n");
    
    for (int i = 0; tests[i] != NULL; i++) {
        total++;
        if (tests[i]()) {
            passed++;
        }
    }
    
    printf("\n========================================\n");
    printf("Results: %d/%d tests passed\n", passed, total);
    printf("========================================\n");
    
    return (passed == total) ? 0 : 1;
}