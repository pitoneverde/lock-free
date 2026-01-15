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
#include <signal.h>
#include <sys/mman.h>
#include <fcntl.h>
#include "simple_mutex.h"

void test_high_contention(int n_threads);

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

void test_high_contention(int n_threads)
{
    printf("=== Test: High Contention (EAGAIN race) ===\n");
    const int ITERATIONS = 100000;
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    atomic_long counter = 0;
    pthread_t threads[n_threads];
    test_args_t args = {&mutex, &counter, ITERATIONS, NULL};
    
    // Create many threads to cause contention
    for (int i = 0; i < n_threads; i++) {
        pthread_create(&threads[i], NULL, high_contention_worker, &args);
    }
    
    // Wait for threads
    for (int i = 0; i < n_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Verify
    long expected = n_threads * ITERATIONS;
    TEST_ASSERT(atomic_load(&counter) == expected, 
                "counter should equal total iterations under high contention");
    
    simple_mutex_destroy(&mutex);
    TEST_PASS("high_contention");
}

void stress_high_contention()
{
	int num_threads[] = {4, 8, 16, 32};

	for (int i = 0; i < 4; i++) {
		test_high_contention(num_threads[i]);
    }
}

// ==================== Test for Spurious Wakeups ====================

/*
 * Spurious wakeup test for futex-based mutexes.
 * 
 * According to futex(2) man page:
 * "In both cases, a FUTEX_WAIT operation can wake up spuriously, 
 * for example because of a signal or due to a spurious wakeup 
 * by the kernel."
 * 
 * This test verifies that our mutex implementation correctly
 * handles spurious wakeups by re-checking the condition and
 * re-entering the wait if necessary.
 */

// Global test state
typedef struct {
    simple_mutex_t *mutex;
    atomic_int *waiters_ready;
    atomic_int *wakeup_count;
    atomic_int *should_exit;
    atomic_int *signal_sent;
} spurious_test_args_t;

// Signal handler to cause EINTR
static volatile atomic_int sigusr1_received = 0;
static void handle_sigusr1(int sig) {
	(void)sig;
    atomic_fetch_add(&sigusr1_received, 1);
}

// Thread that waits on the mutex
void *spurious_wait_thread(void *arg) {
    spurious_test_args_t *args = (spurious_test_args_t *)arg;
    
    // Signal that we're ready to wait
    atomic_fetch_add(args->waiters_ready, 1);
    
    // Wait for main thread to signal start
    while (atomic_load(args->signal_sent) == 0) {
        usleep(1000);
    }
    
    // Try to acquire the lock (will block)
    int lock_result = simple_mutex_lock(args->mutex);
    
    // Count wakeups
    if (lock_result == 0) {
        atomic_fetch_add(args->wakeup_count, 1);
        
        // Hold lock briefly
        usleep(10000);
        simple_mutex_unlock(args->mutex);
    } else {
        printf("Thread got error from lock: %d (errno=%d)\n", 
               lock_result, errno);
    }
    
    return NULL;
}

// Test 1: Signal-induced spurious wakeups
void test_spurious_wakeup_by_signal() {
    printf("=== Test: Spurious Wakeup by Signal (EINTR) ===\n");
    
    // Set up signal handler for SIGUSR1
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGUSR1, &sa, NULL);
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    // Lock the mutex so waiters will block
    simple_mutex_lock(&mutex);
    
    const int NUM_WAITERS = 4;
    atomic_int waiters_ready = 0;
    atomic_int wakeup_count = 0;
    atomic_int signal_sent = 0;
    atomic_int should_exit = 0;
    
    pthread_t waiters[NUM_WAITERS];
    spurious_test_args_t args = {
        &mutex, &waiters_ready, &wakeup_count, &should_exit, &signal_sent
    };
    
    // Create waiting threads
    for (int i = 0; i < NUM_WAITERS; i++) {
        pthread_create(&waiters[i], NULL, spurious_wait_thread, &args);
    }
    
    // Wait for all threads to be ready
    while (atomic_load(&waiters_ready) < NUM_WAITERS) {
        usleep(1000);
    }
    
    printf("All %d waiters are ready, sending signals...\n", NUM_WAITERS);
    
    // Signal start
    atomic_store(&signal_sent, 1);
    
    // Send SIGUSR1 to all waiting threads to cause EINTR
    for (int i = 0; i < NUM_WAITERS; i++) {
        pthread_kill(waiters[i], SIGUSR1);
        usleep(5000); // Space out signals
    }
    
    printf("Signals sent. Checking if threads handled spurious wakeups...\n");
    
    // Wait a bit to see if threads spuriously wake up
    usleep(100000); // 100ms
    
    int current_wakeups = atomic_load(&wakeup_count);
    if (current_wakeups > 0) {
        printf("WARNING: %d threads woke up spuriously (they shouldn't have the lock yet)\n", 
               current_wakeups);
    }
    
    // Now release the lock
    printf("Releasing lock...\n");
    simple_mutex_unlock(&mutex);
    
    // Wait for all threads to finish
    for (int i = 0; i < NUM_WAITERS; i++) {
        pthread_join(waiters[i], NULL);
    }
    
    // Verify final state
    int final_wakeups = atomic_load(&wakeup_count);
    printf("Total threads that acquired lock: %d (expected %d)\n", 
           final_wakeups, NUM_WAITERS);
    
    // Note: Due to signals, some threads might not have acquired the lock
    // if they got EINTR and didn't retry properly
    if (final_wakeups == NUM_WAITERS) {
        printf("PASS: All threads correctly handled spurious wakeups\n");
    } else if (final_wakeups > 0) {
        printf("PARTIAL: Some threads handled spurious wakeups\n");
    } else {
        printf("FAIL: No threads acquired the lock after signals\n");
    }
    
    simple_mutex_destroy(&mutex);
}

// ==================== Test 2: Manual Futex Wake ====================

/*
 * This test manually calls futex(FUTEX_WAKE) on the mutex word
 * while threads are waiting, simulating a spurious wakeup.
 * This is more direct than using signals.
 */

#include <linux/futex.h>
#include <sys/syscall.h>

static long sys_futex(void *addr1, int op, int val1, 
                     struct timespec *timeout, void *addr2, int val3) {
    return syscall(SYS_futex, addr1, op, val1, timeout, addr2, val3);
}

void *manual_spurious_wait_thread(void *arg) {
    spurious_test_args_t *args = (spurious_test_args_t *)arg;
    
    atomic_fetch_add(args->waiters_ready, 1);
    
    // Wait for main thread to start test
    while (atomic_load(args->signal_sent) == 0) {
        usleep(1000);
    }
    
    int attempts = 0;
    int max_attempts = 100;
    
    while (atomic_load(args->should_exit) == 0 && attempts < max_attempts) {
        attempts++;
        
        // Try to acquire lock
        int rc = simple_mutex_lock(args->mutex);
        if (rc == 0) {
            // Got the lock!
            atomic_fetch_add(args->wakeup_count, 1);
            usleep(1000); // Hold briefly
            
            // Check if this was a legitimate acquisition
            if (atomic_load(args->should_exit) == 0) {
                printf("Thread acquired lock spuriously! (attempt %d)\n", attempts);
            }
            
            simple_mutex_unlock(args->mutex);
            break;
        }
        
        usleep(1000); // Small delay between attempts
    }
    
    return NULL;
}

void test_manual_spurious_wakeup() {
    printf("\n=== Test: Manual Spurious Wakeup (futex wake) ===\n");
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    // Lock mutex so threads will wait
    simple_mutex_lock(&mutex);
    
    const int NUM_WAITERS = 3;
    atomic_int waiters_ready = 0;
    atomic_int wakeup_count = 0;
    atomic_int should_exit = 0;
    atomic_int signal_sent = 0;
    
    pthread_t waiters[NUM_WAITERS];
    spurious_test_args_t args = {
        &mutex, &waiters_ready, &wakeup_count, &should_exit, &signal_sent
    };
    
    // Create waiting threads
    for (int i = 0; i < NUM_WAITERS; i++) {
        pthread_create(&waiters[i], NULL, manual_spurious_wait_thread, &args);
    }
    
    // Wait for all threads to be ready
    while (atomic_load(&waiters_ready) < NUM_WAITERS) {
        usleep(1000);
    }
    
    printf("All %d waiters ready. Sending manual futex wakes...\n", NUM_WAITERS);
    atomic_store(&signal_sent, 1);
    
    // Send several manual futex wakes to simulate spurious wakeups
    for (int i = 0; i < 10; i++) {
        // Directly call futex wake on the mutex word
        // This should cause waiting threads to wake up, but they should
        // re-check the mutex state and go back to waiting
        sys_futex(&mutex.word, FUTEX_WAKE, 1, NULL, NULL, 0);
        
        usleep(50000); // 50ms between wakeups
        
        int current_wakeups = atomic_load(&wakeup_count);
        if (current_wakeups > 0) {
            printf("After %d manual wakes: %d threads acquired lock (BAD!)\n",
                   i + 1, current_wakeups);
        }
    }
    
    // Now legitimately release the lock
    printf("Legitimately releasing lock...\n");
    simple_mutex_unlock(&mutex);
    
    // Signal threads to exit
    atomic_store(&should_exit, 1);
    
    // Wait for threads
    for (int i = 0; i < NUM_WAITERS; i++) {
        pthread_join(waiters[i], NULL);
    }
    
    int final_wakeups = atomic_load(&wakeup_count);
    printf("Final: %d threads acquired lock (expected 1-3 depending on scheduling)\n",
           final_wakeups);
    
    if (final_wakeups >= 1 && final_wakeups <= NUM_WAITERS) {
        printf("PASS: Mutex correctly handled manual spurious wakeups\n");
    } else {
        printf("FAIL: Unexpected number of wakeups: %d\n", final_wakeups);
    }
    
    simple_mutex_destroy(&mutex);
}

// ==================== Test 3: High Contention + Signals ====================

/*
 * Stress test with many threads and signals to maximize
 * chances of spurious wakeups.
 */

void *stress_wait_thread(void *arg) {
    spurious_test_args_t *args = (spurious_test_args_t *)arg;
    int local_wake_count = 0;
    
    for (int i = 0; i < 100; i++) {
        simple_mutex_lock(args->mutex);
        // Critical section
        usleep(100); // 100µs
        
        // Check if we woke up spuriously in previous iteration
        if (local_wake_count > i + 1) {
            printf("Thread detected possible spurious wakeup pattern\n");
        }
        
        local_wake_count++;
        atomic_fetch_add(args->wakeup_count, 1);
        
        simple_mutex_unlock(args->mutex);
        
        // Small delay between iterations
        usleep(1000);
    }
    
    return NULL;
}

void test_stress_spurious_wakeups() {
    printf("\n=== Test: Stress Test for Spurious Wakeups ===\n");
    
    simple_mutex_t mutex;
    simple_mutex_init(&mutex);
    
    const int NUM_THREADS = 8;
    const int SIGNALS_PER_THREAD = 10;
    
    atomic_int wakeup_count = 0;
    pthread_t threads[NUM_THREADS];
    
    spurious_test_args_t args = {
        &mutex, NULL, &wakeup_count, NULL, NULL
    };
    
    // Set up signal handler
    struct sigaction sa;
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART; // Important: use restarting syscalls if possible
    sigaction(SIGUSR1, &sa, NULL);
    
    // Create stress threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_create(&threads[i], NULL, stress_wait_thread, &args);
    }
    
    // Send random signals to threads during execution
    for (int s = 0; s < SIGNALS_PER_THREAD * NUM_THREADS; s++) {
        int thread_idx = rand() % NUM_THREADS;
        pthread_kill(threads[thread_idx], SIGUSR1);
        usleep(rand() % 1000); // Random delay 0-1ms
    }
    
    // Wait for all threads
    for (int i = 0; i < NUM_THREADS; i++) {
        pthread_join(threads[i], NULL);
    }
    
    int total_operations = atomic_load(&wakeup_count);
    int expected_operations = NUM_THREADS * 100;
    
    printf("Total lock operations: %d (expected %d)\n", 
           total_operations, expected_operations);
    
    if (total_operations == expected_operations) {
        printf("PASS: All operations completed despite signals\n");
    } else {
        printf("FAIL: Missing %d operations (spurious wakeups may have caused errors)\n",
               expected_operations - total_operations);
    }
    
    printf("Signals received: %d\n", atomic_load(&sigusr1_received));
    
    simple_mutex_destroy(&mutex);
}

// // ==================== Test 4: Futex WAIT vs WAIT_BITSET ====================

// /*
//  * Test to demonstrate the difference between FUTEX_WAIT and FUTEX_WAIT_BITSET
//  * regarding signal handling.
//  * 
//  * FUTEX_WAIT_BITSET with FUTEX_BITSET_MATCH_ANY doesn't restart on EINTR,
//  * making it better for signal-heavy environments.
//  */

// void test_futex_wait_bitset_demo() {
//     printf("\n=== Demo: FUTEX_WAIT vs FUTEX_WAIT_BITSET for Signal Handling ===\n");
    
//     printf("\nImportant notes about spurious wakeups:\n");
//     printf("1. FUTEX_WAIT can return EINTR if interrupted by a signal\n");
//     printf("2. FUTEX_WAIT_BITSET with FUTEX_BITSET_MATCH_ANY ignores signals\n");
//     printf("3. Your mutex implementation should:\n");
//     printf("   - Check condition again after wakeup (mandatory)\n");
//     printf("   - Handle EINTR by retrying (if using FUTEX_WAIT)\n");
//     printf("   - Consider FUTEX_WAIT_BITSET for signal-heavy apps\n");
    
//     printf("\nExample robust pattern for your simple_mutex_lock():\n");
//     printf("```c\n");
//     printf("while (atomic_load(&mutex->word) & _LOCKED) {\n");
//     printf("    // Use FUTEX_WAIT_BITSET to avoid EINTR\n");
//     printf("    syscall(SYS_futex, &mutex->word, FUTEX_WAIT_BITSET,\n");
//     printf("            expected_state, NULL, NULL, FUTEX_BITSET_MATCH_ANY);\n");
//     printf("    // Always re-check condition after futex returns\n");
//     printf("}\n");
//     printf("```\n");
// }

// ==================== Integration with Main Test Suite ====================

void run_all_spurious_wakeup_tests() {
    printf("=========================================\n");
    printf("SPURIOUS WAKEUP TESTS FOR simple_mutex_t\n");
    printf("=========================================\n\n");
    
    // Reset signal counter
    atomic_store(&sigusr1_received, 0);
    
    // Run tests
    test_spurious_wakeup_by_signal();
    test_manual_spurious_wakeup();
    test_stress_spurious_wakeups();
    // test_futex_wait_bitset_demo();
    
    printf("\n=========================================\n");
    printf("SUMMARY: Spurious wakeup handling verification\n");
    printf("=========================================\n");
    printf("Your mutex must:\n");
    printf("1. Re-check the lock state after EVERY futex wakeup\n");
    printf("2. Handle EINTR properly (retry or use FUTEX_WAIT_BITSET)\n");
    printf("3. Never assume a wakeup means the lock is available\n");
    printf("4. Implement the 'loop around futex' pattern correctly\n");
}

int main() {
    run_all_spurious_wakeup_tests();
    return 0;
}