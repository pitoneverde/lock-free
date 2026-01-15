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
#include <math.h>
#include "simple_mutex.h"

// ==================== Benchmark Configuration ====================

#define CACHELINE_SIZE 64
#define ALIGN_AS_CACHELINE __attribute__((aligned(CACHELINE_SIZE)))

typedef struct {
    ALIGN_AS_CACHELINE atomic_long counter;
    ALIGN_AS_CACHELINE char padding[CACHELINE_SIZE - sizeof(atomic_long)];
} padded_counter_t;

typedef struct {
    pthread_t thread;
    simple_mutex_t *mutex;
    pthread_mutex_t *pthread_mutex;
    padded_counter_t *counters;  // Per-thread counters for fairness test
    atomic_long *shared_counter;
    atomic_int *start_flag;
    atomic_int *stop_flag;
    int thread_id;
    int iterations;
    long duration_ms;
    int use_simple_mutex;
} benchmark_args_t;

// ==================== Timing Utilities ====================

static inline uint64_t get_nanotime(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline void spin_wait_ns(uint64_t ns) {
    uint64_t start = get_nanotime();
    while (get_nanotime() - start < ns) {
        __asm__ volatile("pause" ::: "memory");
    }
}

// ==================== Test 1: Uncontended Latency ====================

void benchmark_uncontended_latency(int iterations, int warmup_iterations) {
    printf("\n=========================================\n");
    printf("BENCHMARK 1: Uncontended Latency\n");
    printf("=========================================\n\n");
    
    // Test simple_mutex_t
    {
        simple_mutex_t mutex;
        simple_mutex_init(&mutex);
        
        // Warmup
        for (int i = 0; i < warmup_iterations; i++) {
            simple_mutex_lock(&mutex);
            simple_mutex_unlock(&mutex);
        }
        
        // Actual measurement
        uint64_t start = get_nanotime();
        for (int i = 0; i < iterations; i++) {
            simple_mutex_lock(&mutex);
            simple_mutex_unlock(&mutex);
        }
        uint64_t end = get_nanotime();
        
        double elapsed_ns = (double)(end - start);
        double avg_ns = elapsed_ns / iterations;
        
        printf("simple_mutex_t:\n");
        printf("  Iterations: %d\n", iterations);
        printf("  Total time: %.2f ns\n", elapsed_ns);
        printf("  Avg latency: %.2f ns per lock/unlock pair\n", avg_ns);
        printf("  Throughput: %.2f M ops/sec\n", 
               (iterations / (elapsed_ns / 1e9)) / 1e6);
        
        simple_mutex_destroy(&mutex);
    }
    
    // Test pthread_mutex_t (default, no adaptive spin)
    {
        pthread_mutex_t mutex;
        pthread_mutex_init(&mutex, NULL);
        
        // Warmup
        for (int i = 0; i < warmup_iterations; i++) {
            pthread_mutex_lock(&mutex);
            pthread_mutex_unlock(&mutex);
        }
        
        // Actual measurement
        uint64_t start = get_nanotime();
        for (int i = 0; i < iterations; i++) {
            pthread_mutex_lock(&mutex);
            pthread_mutex_unlock(&mutex);
        }
        uint64_t end = get_nanotime();
        
        double elapsed_ns = (double)(end - start);
        double avg_ns = elapsed_ns / iterations;
        
        printf("\npthread_mutex_t (default):\n");
        printf("  Iterations: %d\n", iterations);
        printf("  Total time: %.2f ns\n", elapsed_ns);
        printf("  Avg latency: %.2f ns per lock/unlock pair\n", avg_ns);
        printf("  Throughput: %.2f M ops/sec\n", 
               (iterations / (elapsed_ns / 1e9)) / 1e6);
        
        pthread_mutex_destroy(&mutex);
    }
    
    // Test pthread_mutex_t with PTHREAD_MUTEX_ADAPTIVE_NP if available
#ifdef PTHREAD_MUTEX_ADAPTIVE_NP
    {
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ADAPTIVE_NP);
        
        pthread_mutex_t mutex;
        pthread_mutex_init(&mutex, &attr);
        
        // Warmup
        for (int i = 0; i < warmup_iterations; i++) {
            pthread_mutex_lock(&mutex);
            pthread_mutex_unlock(&mutex);
        }
        
        // Actual measurement
        uint64_t start = get_nanotime();
        for (int i = 0; i < iterations; i++) {
            pthread_mutex_lock(&mutex);
            pthread_mutex_unlock(&mutex);
        }
        uint64_t end = get_nanotime();
        
        double elapsed_ns = (double)(end - start);
        double avg_ns = elapsed_ns / iterations;
        
        printf("\npthread_mutex_t (adaptive):\n");
        printf("  Iterations: %d\n", iterations);
        printf("  Total time: %.2f ns\n", elapsed_ns);
        printf("  Avg latency: %.2f ns per lock/unlock pair\n", avg_ns);
        printf("  Throughput: %.2f M ops/sec\n", 
               (iterations / (elapsed_ns / 1e9)) / 1e6);
        
        pthread_mutex_destroy(&mutex);
        pthread_mutexattr_destroy(&attr);
    }
#endif
}

// ==================== Test 2: Throughput Under Contention ====================

void *throughput_worker(void *arg) {
    benchmark_args_t *args = (benchmark_args_t *)arg;
    
    // Wait for start signal
    while (atomic_load(args->start_flag) == 0) {
        __asm__ volatile("pause" ::: "memory");
    }
    
    uint64_t start_time = get_nanotime();
    uint64_t duration_ns = args->duration_ms * 1000000ULL;
    long local_counter = 0;
    
    while (get_nanotime() - start_time < duration_ns) {
        if (args->use_simple_mutex) {
            simple_mutex_lock(args->mutex);
        } else {
            pthread_mutex_lock(args->pthread_mutex);
        }
        
        // Critical section: increment shared counter
        atomic_fetch_add(args->shared_counter, 1);
        
        if (args->use_simple_mutex) {
            simple_mutex_unlock(args->mutex);
        } else {
            pthread_mutex_unlock(args->pthread_mutex);
        }
        
        local_counter++;
    }
    
    // Store local counter for per-thread stats
    if (args->counters) {
        atomic_store(&args->counters[args->thread_id].counter, local_counter);
    }
    
    return NULL;
}

void benchmark_throughput_curve(int min_threads, int max_threads, int duration_ms) {
    printf("\n=========================================\n");
    printf("BENCHMARK 2: Throughput Curve (%d ms per test)\n", duration_ms);
    printf("=========================================\n\n");
    
    printf("Threads | simple_mutex_t (M ops/sec) | pthread_mutex_t (M ops/sec) | Ratio\n");
    printf("--------|----------------------------|----------------------------|------\n");
    
    for (int num_threads = min_threads; num_threads <= max_threads; num_threads++) {
        double simple_throughput = 0, pthread_throughput = 0;
        
        // Test simple_mutex_t
        {
            simple_mutex_t mutex;
            simple_mutex_init(&mutex);
            
            atomic_long shared_counter = 0;
            atomic_int start_flag = 0;
            
            benchmark_args_t args[num_threads];
            pthread_t threads[num_threads];
            
            // Initialize worker arguments
            for (int i = 0; i < num_threads; i++) {
                args[i].mutex = &mutex;
                args[i].pthread_mutex = NULL;
                args[i].shared_counter = &shared_counter;
                args[i].start_flag = &start_flag;
                args[i].stop_flag = NULL;
                args[i].thread_id = i;
                args[i].duration_ms = duration_ms;
                args[i].use_simple_mutex = 1;
                args[i].counters = NULL;
                
                pthread_create(&threads[i], NULL, throughput_worker, &args[i]);
            }
            
            // Let threads start
            usleep(10000);  // 10ms
            atomic_store(&start_flag, 1);
            
            // Wait for threads
            for (int i = 0; i < num_threads; i++) {
                pthread_join(threads[i], NULL);
            }
            
            // Calculate throughput
            long total_ops = atomic_load(&shared_counter);
            double seconds = duration_ms / 1000.0;
            simple_throughput = total_ops / seconds / 1e6;  // M ops/sec
            
            simple_mutex_destroy(&mutex);
        }
        
        // Test pthread_mutex_t
        {
            pthread_mutex_t mutex;
            pthread_mutex_init(&mutex, NULL);
            
            atomic_long shared_counter = 0;
            atomic_int start_flag = 0;
            
            benchmark_args_t args[num_threads];
            pthread_t threads[num_threads];
            
            // Initialize worker arguments
            for (int i = 0; i < num_threads; i++) {
                args[i].mutex = NULL;
                args[i].pthread_mutex = &mutex;
                args[i].shared_counter = &shared_counter;
                args[i].start_flag = &start_flag;
                args[i].stop_flag = NULL;
                args[i].thread_id = i;
                args[i].duration_ms = duration_ms;
                args[i].use_simple_mutex = 0;
                args[i].counters = NULL;
                
                pthread_create(&threads[i], NULL, throughput_worker, &args[i]);
            }
            
            // Let threads start
            usleep(10000);  // 10ms
            atomic_store(&start_flag, 1);
            
            // Wait for threads
            for (int i = 0; i < num_threads; i++) {
                pthread_join(threads[i], NULL);
            }
            
            // Calculate throughput
            long total_ops = atomic_load(&shared_counter);
            double seconds = duration_ms / 1000.0;
            pthread_throughput = total_ops / seconds / 1e6;  // M ops/sec
            
            pthread_mutex_destroy(&mutex);
        }
        
        double ratio = pthread_throughput > 0 ? simple_throughput / pthread_throughput : 0;
        printf("%7d | %26.2f | %26.2f | %.2fx\n", 
               num_threads, simple_throughput, pthread_throughput, ratio);
    }
}

// ==================== Test 3: Fairness Measurement ====================

/*
 * Since your mutex is not fair, this test measures how unfair it is.
 * We'll run multiple threads competing for the lock and measure
 * how many times each thread acquires it.
 */

void benchmark_fairness(int num_threads, int duration_ms) {
    printf("\n=========================================\n");
    printf("BENCHMARK 3: Fairness Test (%d threads, %d ms)\n", num_threads, duration_ms);
    printf("=========================================\n\n");
    
    printf("Testing simple_mutex_t (not fair by design):\n");
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    atomic_long shared_counter = 0;
    atomic_int start_flag = 0;
    
    // Allocate per-thread counters (cache-line aligned to avoid false sharing)
    padded_counter_t *per_thread_counters = calloc(num_threads, sizeof(padded_counter_t));
    
    benchmark_args_t args[num_threads];
    pthread_t threads[num_threads];
    
    // Initialize worker arguments
    for (int i = 0; i < num_threads; i++) {
        args[i].mutex = &mutex;
        args[i].pthread_mutex = NULL;
        args[i].shared_counter = &shared_counter;
        args[i].start_flag = &start_flag;
        args[i].stop_flag = NULL;
        args[i].thread_id = i;
        args[i].duration_ms = duration_ms;
        args[i].use_simple_mutex = 1;
        args[i].counters = per_thread_counters;
        
        pthread_create(&threads[i], NULL, throughput_worker, &args[i]);
    }
    
    // Let threads start
    usleep(10000);  // 10ms
    atomic_store(&start_flag, 1);
    
    // Wait for threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Calculate statistics
    long min_ops = LONG_MAX, max_ops = 0, total_ops = 0;
    long thread_ops[num_threads];
    
    printf("Thread acquisitions:\n");
    for (int i = 0; i < num_threads; i++) {
        thread_ops[i] = atomic_load(&per_thread_counters[i].counter);
        total_ops += thread_ops[i];
        
        if (thread_ops[i] < min_ops) min_ops = thread_ops[i];
        if (thread_ops[i] > max_ops) max_ops = thread_ops[i];
        
        printf("  Thread %2d: %10ld ops\n", i, thread_ops[i]);
    }
    
    // Calculate fairness metrics
    double avg_ops = (double)total_ops / num_threads;
    double stddev = 0;
    for (int i = 0; i < num_threads; i++) {
        double diff = thread_ops[i] - avg_ops;
        stddev += diff * diff;
    }
    stddev = sqrt(stddev / num_threads);
    
    double fairness_ratio = min_ops / (double)max_ops;
    
    printf("\nFairness statistics:\n");
    printf("  Minimum acquisitions: %ld\n", min_ops);
    printf("  Maximum acquisitions: %ld\n", max_ops);
    printf("  Average acquisitions: %.0f\n", avg_ops);
    printf("  Standard deviation: %.0f\n", stddev);
    printf("  Fairness ratio (min/max): %.3f (1.0 = perfectly fair)\n", fairness_ratio);
    printf("  Coefficient of variation: %.3f%%\n", (stddev / avg_ops) * 100);
    
    // Compare with pthread_mutex_t
    printf("\n\nTesting pthread_mutex_t (for comparison):\n");
    
    pthread_mutex_t pthread_mutex;
    pthread_mutex_init(&pthread_mutex, NULL);
    
    atomic_store(&shared_counter, 0);
    atomic_store(&start_flag, 0);
    
    // Reset counters
    for (int i = 0; i < num_threads; i++) {
        atomic_store(&per_thread_counters[i].counter, 0);
    }
    
    // Re-initialize with pthread mutex
    for (int i = 0; i < num_threads; i++) {
        args[i].mutex = NULL;
        args[i].pthread_mutex = &pthread_mutex;
        args[i].use_simple_mutex = 0;
        
        pthread_create(&threads[i], NULL, throughput_worker, &args[i]);
    }
    
    // Let threads start
    usleep(10000);  // 10ms
    atomic_store(&start_flag, 1);
    
    // Wait for threads
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Calculate statistics for pthread
    long pthread_min_ops = LONG_MAX, pthread_max_ops = 0, pthread_total_ops = 0;
    
    printf("Thread acquisitions (pthread):\n");
    for (int i = 0; i < num_threads; i++) {
        long ops = atomic_load(&per_thread_counters[i].counter);
        pthread_total_ops += ops;
        
        if (ops < pthread_min_ops) pthread_min_ops = ops;
        if (ops > pthread_max_ops) pthread_max_ops = ops;
        
        printf("  Thread %2d: %10ld ops\n", i, ops);
    }
    
    double pthread_fairness_ratio = pthread_min_ops / (double)pthread_max_ops;
    
    printf("\nPthread fairness ratio (min/max): %.3f\n", pthread_fairness_ratio);
    
    // Cleanup
    free(per_thread_counters);
    simple_mutex_destroy(&mutex);
    pthread_mutex_destroy(&pthread_mutex);
}

// ==================== Test 4: Critical Section Size Sensitivity ====================

/*
 * Tests how the mutex performs with different critical section sizes.
 * This shows the overhead relative to the protected work.
 */

void *variable_cs_worker(void *arg) {
    benchmark_args_t *args = (benchmark_args_t *)arg;
    int cs_size_ns = args->iterations;  // Reusing iterations field as CS size
    
    // Wait for start signal
    while (atomic_load(args->start_flag) == 0) {
        __asm__ volatile("pause" ::: "memory");
    }
    
    uint64_t start_time = get_nanotime();
    uint64_t duration_ns = args->duration_ms * 1000000ULL;
    long local_counter = 0;
    
    while (get_nanotime() - start_time < duration_ns) {
        if (args->use_simple_mutex) {
            simple_mutex_lock(args->mutex);
        } else {
            pthread_mutex_lock(args->pthread_mutex);
        }
        
        // Variable-sized critical section
        spin_wait_ns(cs_size_ns);
        atomic_fetch_add(args->shared_counter, 1);
        
        if (args->use_simple_mutex) {
            simple_mutex_unlock(args->mutex);
        } else {
            pthread_mutex_unlock(args->pthread_mutex);
        }
        
        local_counter++;
    }
    
    return NULL;
}

void benchmark_critical_section_sensitivity(int num_threads, int duration_ms) {
    printf("\n=========================================\n");
    printf("BENCHMARK 4: Critical Section Size Sensitivity\n");
    printf("=========================================\n\n");
    
    int cs_sizes_ns[] = {0, 10, 100, 1000, 10000, 100000};  // 0ns to 100µs
    int num_sizes = sizeof(cs_sizes_ns) / sizeof(cs_sizes_ns[0]);
    
    printf("CS Size (ns) | simple_mutex_t (K ops/sec) | pthread_mutex_t (K ops/sec)\n");
    printf("-------------|----------------------------|----------------------------\n");
    
    for (int s = 0; s < num_sizes; s++) {
        int cs_size_ns = cs_sizes_ns[s];
        
        // Test simple_mutex_t
        double simple_throughput = 0;
        {
            simple_mutex_t mutex;
            simple_mutex_init(&mutex);
            
            atomic_long shared_counter = 0;
            atomic_int start_flag = 0;
            
            benchmark_args_t args[num_threads];
            pthread_t threads[num_threads];
            
            for (int i = 0; i < num_threads; i++) {
                args[i].mutex = &mutex;
                args[i].pthread_mutex = NULL;
                args[i].shared_counter = &shared_counter;
                args[i].start_flag = &start_flag;
                args[i].iterations = cs_size_ns;  // Reuse field for CS size
                args[i].duration_ms = duration_ms;
                args[i].use_simple_mutex = 1;
                
                pthread_create(&threads[i], NULL, variable_cs_worker, &args[i]);
            }
            
            usleep(10000);
            atomic_store(&start_flag, 1);
            
            for (int i = 0; i < num_threads; i++) {
                pthread_join(threads[i], NULL);
            }
            
            long total_ops = atomic_load(&shared_counter);
            simple_throughput = total_ops / (duration_ms / 1000.0) / 1e3;  // K ops/sec
            
            simple_mutex_destroy(&mutex);
        }
        
        // Test pthread_mutex_t
        double pthread_throughput = 0;
        {
            pthread_mutex_t mutex;
            pthread_mutex_init(&mutex, NULL);
            
            atomic_long shared_counter = 0;
            atomic_int start_flag = 0;
            
            benchmark_args_t args[num_threads];
            pthread_t threads[num_threads];
            
            for (int i = 0; i < num_threads; i++) {
                args[i].mutex = NULL;
                args[i].pthread_mutex = &mutex;
                args[i].shared_counter = &shared_counter;
                args[i].start_flag = &start_flag;
                args[i].iterations = cs_size_ns;  // Reuse field for CS size
                args[i].duration_ms = duration_ms;
                args[i].use_simple_mutex = 0;
                
                pthread_create(&threads[i], NULL, variable_cs_worker, &args[i]);
            }
            
            usleep(10000);
            atomic_store(&start_flag, 1);
            
            for (int i = 0; i < num_threads; i++) {
                pthread_join(threads[i], NULL);
            }
            
            long total_ops = atomic_load(&shared_counter);
            pthread_throughput = total_ops / (duration_ms / 1000.0) / 1e3;  // K ops/sec
            
            pthread_mutex_destroy(&mutex);
        }
        
        printf("%12d | %26.1f | %26.1f\n", 
               cs_size_ns, simple_throughput, pthread_throughput);
    }
}

// ==================== Test 5: Memory Overhead ====================

void benchmark_memory_overhead() {
    printf("\n=========================================\n");
    printf("BENCHMARK 5: Memory Overhead\n");
    printf("=========================================\n\n");
    
    printf("Type                | Size (bytes) | Alignment | Cache Lines\n");
    printf("-------------------|--------------|-----------|-------------\n");
    printf("simple_mutex_t     | %12zu | %9d | %12d\n", 
           sizeof(simple_mutex_t), 
           __alignof__(simple_mutex_t),
           (int)(sizeof(simple_mutex_t) + CACHELINE_SIZE - 1) / CACHELINE_SIZE);
    
    printf("pthread_mutex_t    | %12zu | %9zu | %12d\n", 
           sizeof(pthread_mutex_t),
           __alignof__(pthread_mutex_t),
           (int)(sizeof(pthread_mutex_t) + CACHELINE_SIZE - 1) / CACHELINE_SIZE);
    
    // Show layout
    printf("\nLayout of simple_mutex_t:\n");
    printf("  word (uint32_t): offset 0, size 4 bytes\n");
    printf("  padding: offset 4, size %zu bytes (to 64-byte alignment)\n", 
           sizeof(simple_mutex_t) - 4);
}

// ==================== Test 6: Lock/Unlock Pair Breakdown ====================

void benchmark_lock_unlock_breakdown(int iterations) {
    printf("\n=========================================\n");
    printf("BENCHMARK 6: Lock/Unlock Breakdown\n");
    printf("=========================================\n\n");
    
    // Simple microbenchmark to measure lock vs unlock separately
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    // Measure lock time
    uint64_t lock_start = get_nanotime();
    for (int i = 0; i < iterations; i++) {
        simple_mutex_lock(&mutex);
        simple_mutex_unlock(&mutex);  // Unlock immediately
    }
    uint64_t lock_end = get_nanotime();
    
    // Hold lock to measure unlock separately
    simple_mutex_lock(&mutex);
    
    uint64_t unlock_start = get_nanotime();
    for (int i = 0; i < iterations; i++) {
        // Can't measure unlock in isolation easily
        // This just shows the overhead
    }
    uint64_t unlock_end = get_nanotime();
    
    simple_mutex_unlock(&mutex);
    
    double total_ns = (double)(lock_end - lock_start);
    double per_pair_ns = total_ns / iterations;
    
    printf("simple_mutex_t lock/unlock pair breakdown:\n");
    printf("  Total iterations: %d\n", iterations);
    printf("  Total time: %.2f ns\n", total_ns);
    printf("  Average per lock/unlock pair: %.2f ns\n", per_pair_ns);
    printf("\nNote: Futex operations have variable cost depending on contention.\n");
    printf("      Uncontended case uses atomic operations only.\n");
    printf("      Contended case adds syscall overhead (~1000+ ns).\n");
    
    simple_mutex_destroy(&mutex);
}

// ==================== Main Benchmark Runner ====================

int main(int argc, char *argv[]) {
    printf("=========================================\n");
    printf("COMPREHENSIVE PERFORMANCE BENCHMARK SUITE\n");
    printf("=========================================\n");
    printf("Comparing simple_mutex_t vs pthread_mutex_t\n");
    printf("System: %d CPU cores available\n", (int)sysconf(_SC_NPROCESSORS_ONLN));
    printf("=========================================\n");
    
    int warmup_iterations = 100000;
    int measurement_iterations = 10000000;
    
    // 1. Uncontended latency (single thread)
    benchmark_uncontended_latency(measurement_iterations, warmup_iterations);
    
    // 2. Throughput curve (1 to 16 threads)
    benchmark_throughput_curve(1, 16, 2000);  // 2 seconds per test
    
    // 3. Fairness measurement (8 threads)
    benchmark_fairness(8, 3000);  // 3 seconds
    
    // 4. Critical section sensitivity (4 threads)
    benchmark_critical_section_sensitivity(4, 2000);  // 2 seconds per test
    
    // 5. Memory overhead
    benchmark_memory_overhead();
    
    // 6. Lock/unlock breakdown
    benchmark_lock_unlock_breakdown(1000000);
    
    printf("\n=========================================\n");
    printf("SUMMARY: simple_mutex_t Performance Characteristics\n");
    printf("=========================================\n");
    printf("Strengths:\n");
    printf("  - Minimal memory footprint (64 bytes)\n");
    printf("  - Good uncontended performance\n");
    printf("  - Simple implementation\n\n");
    
    printf("Weaknesses (expected):\n");
    printf("  - Poor scalability under contention (no spinning)\n");
    printf("  - Unfair scheduling (can starve threads)\n");
    printf("  - Always syscalls when contended\n");
    printf("  - No owner tracking or reentrancy\n\n");
    
    printf("Expected Results:\n");
    printf("  - Uncontended: Within 2x of pthread_mutex_t\n");
    printf("  - 4 threads: 0.5-0.8x pthread throughput\n");
    printf("  - 16 threads: 0.1-0.3x pthread throughput\n");
    printf("  - Fairness: Ratio < 0.5 (highly unfair)\n");
    
    return 0;
}