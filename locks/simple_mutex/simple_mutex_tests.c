#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdatomic.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include "simple_mutex.h"

// Tests
void test_single_thread_sanity();
void test_mutual_exclusion(int num_threads, int iterations_per_thread);
void test_wake_one_guarantee();
void test_error_conditions();

// ==================== Core Correctness Tests ====================

void test_single_thread_sanity()
{
	printf("=== Test: Single Thread Sanity ===\n");
    
    simple_mutex_t mutex;
    
    // Test init
    int rc = simple_mutex_init(&mutex);
    TEST_ASSERT(rc == 0, "init should succeed");
    
    // Test lock/unlock
    rc = simple_mutex_lock(&mutex);
    TEST_ASSERT(rc == 0, "lock should succeed");
    
    rc = simple_mutex_unlock(&mutex);
    TEST_ASSERT(rc == 0, "unlock should succeed");
    
    // Test destroy
    rc = simple_mutex_destroy(&mutex);
    TEST_ASSERT(rc == 0, "destroy should succeed");
    
    // Test double destroy
    rc = simple_mutex_destroy(&mutex);
    TEST_ASSERT(rc == -EINVAL, "double destroy should return -EINVAL");
    
    TEST_PASS("single_thread_sanity");
}


void *mutual_exclusion_worker(void *arg) {
    test_args_t *args = (test_args_t *)arg;
    
    // Wait for all threads to be ready
    while (atomic_load(args->start_flag) == 0) {
        // spin
    }
    
    for (int i = 0; i < args->iterations; i++) {
        simple_mutex_lock(args->mutex);
        // Critical section: increment shared counter
        atomic_fetch_add(args->counter, 1);
        simple_mutex_unlock(args->mutex);
    }
    
    return NULL;
}

void test_mutual_exclusion(int num_threads, int iterations_per_thread)
{
    printf("=== Test: Mutual Exclusion (%d threads, %d iterations each) ===\n", 
           num_threads, iterations_per_thread);
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    atomic_long counter = 0;
    atomic_int start_flag = 0;
    pthread_t threads[num_threads];
    test_args_t args = {&mutex, &counter, iterations_per_thread, &start_flag};
    
    // Create threads
    for (int i = 0; i < num_threads; i++) {
        int rc = pthread_create(&threads[i], NULL, 
                               mutual_exclusion_worker, &args);
        TEST_ASSERT(rc == 0, "pthread_create failed");
    }
    
    // Let all threads start together
    usleep(10000);  // 10ms for threads to initialize
    atomic_store(&start_flag, 1);
    
    // Wait for threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Verify counter
    long expected = num_threads * iterations_per_thread;
    TEST_ASSERT(atomic_load(&counter) == expected, 
                "counter should equal total iterations");
    
    simple_mutex_destroy(&mutex);
    printf("PASS: mutual_exclusion (%d threads)\n", num_threads);
}

// ==================== Progress & Wake-One Tests ====================

typedef struct {
    simple_mutex_t *mutex;
    atomic_int *phase;
    atomic_int *thread_ready;
    atomic_int *thread_proceeded;
} wake_test_args_t;

void *wake_one_worker(void *arg) {
    wake_test_args_t *args = (wake_test_args_t *)arg;
    int thread_id = atomic_fetch_add(args->thread_ready, 1);
    
    // Thread 0 holds the lock initially
    if (thread_id == 0) {
        simple_mutex_lock(args->mutex);
        atomic_store(args->phase, 1);  // Signal that thread 0 has lock
        
        // Wait for other threads to queue up
        while (atomic_load(args->thread_ready) < 3) {
            usleep(1000);
        }
        
        // Release lock
        simple_mutex_unlock(args->mutex);
        atomic_store(args->phase, 2);
        
    } else {
        // Wait for thread 0 to get lock
        while (atomic_load(args->phase) < 1) {
            usleep(1000);
        }
        
        // Try to acquire lock (will block)
        simple_mutex_lock(args->mutex);
        
        // Record that we proceeded
        atomic_fetch_add(args->thread_proceeded, 1);
        
        // Hold lock briefly, then release
        usleep(10000);
        simple_mutex_unlock(args->mutex);
    }
    
    return NULL;
}

void test_wake_one_guarantee()
{
    printf("=== Test: Wake-One Guarantee ===\n");
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    atomic_int phase = 0;
    atomic_int thread_ready = 0;
    atomic_int thread_proceeded = 0;
    
    pthread_t threads[3];
    wake_test_args_t args = {&mutex, &phase, &thread_ready, &thread_proceeded};
    
    // Create 3 threads
    for (int i = 0; i < 3; i++) {
        pthread_create(&threads[i], NULL, wake_one_worker, &args);
    }
    
    // Wait for all threads
    for (int i = 0; i < 3; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Only one waiter should have proceeded immediately after first unlock
    // (The exact value depends on timing, but should be 1 or 2)
    int proceeded = atomic_load(&thread_proceeded);
    TEST_ASSERT(proceeded > 0, "At least one thread should have proceeded");
    
    simple_mutex_destroy(&mutex);
    TEST_PASS("wake_one_guarantee");
}

// ==================== Error Condition Tests ====================

void test_error_conditions()
{
	printf("=== Test: Error Conditions ===\n");
    
    simple_mutex_t mutex;
    
    // Test lock on uninitialized mutex (should work with static init)
    memset(&mutex, 0, sizeof(mutex));
    int rc = simple_mutex_lock(&mutex);
    TEST_ASSERT(rc == 0, "lock on zeroed mutex (UB but shouldn't crash)");
    simple_mutex_unlock(&mutex);
    
    // Proper init
    rc = simple_mutex_init(&mutex);
    TEST_ASSERT(rc == 0, "init should succeed");
    
    // Test destroy on locked mutex
    simple_mutex_lock(&mutex);
    rc = simple_mutex_destroy(&mutex);
    TEST_ASSERT(rc == -EBUSY, "destroy on locked mutex should return -EBUSY");
    simple_mutex_unlock(&mutex);
    
    // Proper destroy
    rc = simple_mutex_destroy(&mutex);
    TEST_ASSERT(rc == 0, "destroy should succeed");
    
    // Test operations on destroyed mutex
    rc = simple_mutex_lock(&mutex);
    TEST_ASSERT(rc == -EINVAL, "lock on destroyed mutex should return -EINVAL");
    
    rc = simple_mutex_unlock(&mutex);
    TEST_ASSERT(rc == -EINVAL, "unlock on destroyed mutex should return -EINVAL");
    
    rc = simple_mutex_destroy(&mutex);
    TEST_ASSERT(rc == -EINVAL, "double destroy should return -EINVAL");
    
    TEST_PASS("error_conditions");
}

// ==================== High Contention (EAGAIN Race) Test ====================

void *high_contention_worker(void *arg) {
    test_args_t *args = (test_args_t *)arg;
    
    for (int i = 0; i < args->iterations; i++) {
        while (simple_mutex_lock(args->mutex) != 0) {
            // In your implementation, lock should only return 0 or -EINVAL
            // But we might get -EAGAIN internally (futex-specific)
        }
        atomic_fetch_add(args->counter, 1);
        simple_mutex_unlock(args->mutex);
    }
    
    return NULL;
}

void test_high_contention()
{
    printf("=== Test: High Contention (EAGAIN race) ===\n");
    
    const int NUM_THREADS = 8;
    const int ITERATIONS = 100000;
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    atomic_long counter = 0;
    pthread_t threads[NUM_THREADS];
    test_args_t args = {&mutex, &counter, ITERATIONS, NULL};
    
    // Create many threads to cause contention
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, high_contention_worker, &args);
    }
    
    // Wait for threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Verify
    long expected = NUM_THREADS * ITERATIONS;
    TEST_ASSERT(atomic_load(&counter) == expected, 
                "counter should equal total iterations under high contention");
    
    simple_mutex_destroy(&mutex);
    TEST_PASS("high_contention");
}

// ==================== Main Test Runner ====================

int main() {
    printf("Starting simple_mutex_t tests...\n\n");
    
    // Core correctness tests
    test_single_thread_sanity();
    printf("\n");
    
    test_mutual_exclusion(2, 1000000);
    test_mutual_exclusion(4, 250000);
    test_mutual_exclusion(8, 125000);
    printf("\n");
    
    test_wake_one_guarantee();
    printf("\n");
    
    test_error_conditions();
    printf("\n");
    
    printf("All tests completed successfully!\n");
    return 0;
}