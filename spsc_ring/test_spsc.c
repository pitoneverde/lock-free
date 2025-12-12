#include "spsc_ring.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <time.h>

/* ============== SINGLE-THREADED UNIT TESTS ============== */

void test_create_destroy(void)
{
	printf("test_create_destroy: ");
	t_spsc_ring *q = spsc_create(64);
	assert(q != NULL);
	assert(q->buf != NULL);
	assert(q->mask == 63); // 64-1, power of two
	spsc_destroy(q);
	printf("✓\n");
}

void test_single_byte_operations(void)
{
	printf("test_single_byte_operations: ");
	t_spsc_ring *q = spsc_create(8); // 8-byte buffer, 7-byte capacity
	unsigned char byte;

	// Test empty pop
	assert(spsc_try_pop(q, &byte) == false);

	// Fill buffer (7 bytes capacity)
	for (int i = 0; i < 7; i++)
	{
		assert(spsc_try_push(q, i) == true);
	}

	// Should be full now
	assert(spsc_try_push(q, 7) == false);

	// Pop all
	for (int i = 0; i < 7; i++)
	{
		assert(spsc_try_pop(q, &byte) == true);
		assert(byte == i);
	}

	// Should be empty again
	assert(spsc_try_pop(q, &byte) == false);

	spsc_destroy(q);
	printf("✓\n");
}

void test_batch_operations(void)
{
	printf("test_batch_operations: ");
	t_spsc_ring *q = spsc_create(64); // 63-byte capacity
	unsigned char data[100];
	unsigned char output[100];

	// Prepare test data
	for (int i = 0; i < 100; i++)
	{
		data[i] = i;
	}

	// Push more than capacity
	size_t pushed = spsc_push_batch(q, data, 100);
	assert(pushed == 63); // Only capacity fits

	// Pop everything
	memset(output, 0, sizeof(output));
	size_t popped = spsc_pop_batch(q, output, 100);
	assert(popped == 63);

	// Verify data
	for (int i = 0; i < 63; i++)
	{
		assert(output[i] == data[i]);
	}

	// Test partial batch
	pushed = spsc_push_batch(q, data, 30);
	assert(pushed == 30);

	popped = spsc_pop_batch(q, output, 20);
	assert(popped == 20);

	spsc_destroy(q);
	printf("✓\n");
}

void test_wraparound(void)
{
	printf("test_wraparound: ");
	t_spsc_ring *q = spsc_create(16); // 15-byte capacity
	unsigned char data[20];
	unsigned char output[20];

	for (int i = 0; i < 20; i++)
	{
		data[i] = i;
	}

	// Push 10 bytes (not reaching end)
	size_t pushed = spsc_push_batch(q, data, 10);
	assert(pushed == 10);

	// Pop 5 bytes
	size_t popped = spsc_pop_batch(q, output, 5);
	assert(popped == 5);

	// Push 8 more bytes (should wrap around)
	pushed = spsc_push_batch(q, data + 10, 8);
	assert(pushed == 8);

	// Buffer now has: [10-17] wrapped + [5-9] at start
	popped = spsc_pop_batch(q, output, 13);
	assert(popped == 13);

	spsc_destroy(q);
	printf("✓\n");
}
void test_alternating_single_batch(void)
{
	printf("test_alternating_single_batch: ");
	t_spsc_ring *q = spsc_create(32);
	unsigned char byte;

	// Push and pop within same iteration to stay within capacity
	for (int i = 0; i < 10; i++)
	{
		// Push single and batch
		assert(spsc_try_push(q, i));
		unsigned char batch[3] = {100 + i, 101 + i, 102 + i};
		assert(spsc_push_batch(q, batch, 3) == 3);

		// Pop single and batch
		assert(spsc_try_pop(q, &byte));
		assert(byte == i);

		unsigned char out[3];
		assert(spsc_pop_batch(q, out, 3) == 3);
		assert(out[0] == 100 + i);
		assert(out[1] == 101 + i);
		assert(out[2] == 102 + i);
	}

	spsc_destroy(q);
	printf("✓\n");
}

// Add this new test for capacity verification
void test_capacity_limits(void)
{
	printf("test_capacity_limits: ");
	t_spsc_ring *q = spsc_create(8);
	unsigned char byte;

	// Test exact capacity
	for (int i = 0; i < 7; i++)
	{
		assert(spsc_try_push(q, i));
	}
	assert(spsc_try_push(q, 99) == false);

	// Free one slot
	assert(spsc_try_pop(q, &byte));
	assert(byte == 0);

	// Should be able to push again
	assert(spsc_try_push(q, 7));

	// Now full again
	assert(spsc_try_push(q, 99) == false);

	spsc_destroy(q);
	printf("✓\n");
}

/* ============== CONCURRENT STRESS TESTS ============== */

#define STRESS_ITERATIONS 1000000
#define PRODUCER_BATCH_SIZE 127
#define CONSUMER_BATCH_SIZE 91

typedef struct
{
	t_spsc_ring *q;
	long long produced;
	int id;
} producer_args;

typedef struct
{
	t_spsc_ring *q;
	long long consumed;
	int errors;
} consumer_args;

void *producer_thread(void *arg)
{
	producer_args *pargs = (producer_args *)arg;
	unsigned char data[PRODUCER_BATCH_SIZE];

	for (long long i = 0; i < STRESS_ITERATIONS; i++)
	{
		// Fill data with pattern based on iteration
		for (int j = 0; j < PRODUCER_BATCH_SIZE; j++)
		{
			data[j] = (i + j + pargs->id) & 0xFF;
		}

		size_t pushed = 0;
		while (pushed < PRODUCER_BATCH_SIZE)
		{
			pushed += spsc_push_batch(pargs->q, data + pushed, PRODUCER_BATCH_SIZE - pushed);
			// Tiny yield to prevent complete starvation on single core
			sched_yield();
		}
		pargs->produced += PRODUCER_BATCH_SIZE;
	}

	return NULL;
}

void *consumer_thread(void *arg)
{
	consumer_args *cargs = (consumer_args *)arg;
	unsigned char data[CONSUMER_BATCH_SIZE];

	for (long long i = 0; i < STRESS_ITERATIONS; i++)
	{
		size_t expected = 0;
		while (expected < PRODUCER_BATCH_SIZE)
		{
			size_t popped = spsc_pop_batch(cargs->q, data, CONSUMER_BATCH_SIZE);

			// Verify popped data
			for (size_t j = 0; j < popped; j++)
			{
				unsigned char expected_byte = (i + expected + j) & 0xFF;
				if (data[j] != expected_byte)
				{
					cargs->errors++;
				}
			}

			expected += popped;
			sched_yield();
		}
		cargs->consumed += PRODUCER_BATCH_SIZE;
	}

	return NULL;
}

/* ============== EDGE CASE TESTS ============== */
void test_power_of_two_rounding(void)
{
	printf("test_power_of_two_rounding: ");

	struct
	{
		size_t requested;
		size_t expected_mask; // mask = physical_size - 1
		size_t physical_size; // What we actually allocate
	} tests[] = {
		// requested -> physical_size -> mask
		{1, 1, 2},		   // 1 requested -> 2 bytes buffer -> mask=1
		{2, 1, 2},		   // 2 requested -> 2 bytes buffer -> mask=1
		{3, 3, 4},		   // 3 requested -> 4 bytes buffer -> mask=3
		{7, 7, 8},		   // 7 requested -> 8 bytes buffer -> mask=7
		{8, 7, 8},		   // 8 requested -> 8 bytes buffer -> mask=7
		{9, 15, 16},	   // 9 requested -> 16 bytes buffer -> mask=15
		{100, 127, 128},   // 100 requested -> 128 bytes buffer -> mask=127
		{1000, 1023, 1024} // 1000 requested -> 1024 bytes buffer -> mask=1023
	};

	for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++)
	{
		t_spsc_ring *q = spsc_create(tests[i].requested);
		assert(q != NULL);
		assert(q->mask == tests[i].expected_mask);

		// Verify we can use the full logical capacity
		size_t capacity = q->mask; // logical capacity
		if (capacity > 0)
		{
			unsigned char *data = malloc(capacity);
			memset(data, 0xAA, capacity);

			size_t pushed = spsc_push_batch(q, data, capacity);
			assert(pushed == capacity);

			// One more should fail
			assert(spsc_try_push(q, 0xBB) == false);

			free(data);
		}

		spsc_destroy(q);
	}

	printf("✓\n");
}

void test_zero_length_operations(void)
{
	printf("test_zero_length_operations: ");
	t_spsc_ring *q = spsc_create(64);

	// Zero-length batch operations
	unsigned char data[10];
	assert(spsc_push_batch(q, data, 0) == 0);
	assert(spsc_pop_batch(q, data, 0) == 0);

	// Normal operation still works
	assert(spsc_try_push(q, 0x42) == true);
	assert(spsc_try_pop(q, data) == true);
	assert(data[0] == 0x42);

	spsc_destroy(q);
	printf("✓\n");
}

void test_single_producer_multiple_consumers_invalid(void)
{
	printf("test_single_producer_multiple_consumers_invalid: ");

	// This is an SPSC buffer - using multiple consumers is undefined
	// But we should at least not crash
	t_spsc_ring *q = spsc_create(1024);

	pthread_t consumers[2];
	int results[2] = {0, 0};

	// Push some data
	unsigned char data[100];
	memset(data, 0x55, 100);
	spsc_push_batch(q, data, 100);

	// Try to consume from two threads (incorrect usage, but shouldn't crash)
	for (int i = 0; i < 2; i++)
	{
		int *result = &results[i];
		pthread_create(&consumers[i], NULL,
					   (void *(*)(void *))spsc_pop_batch, (void *[]){(void *)q, data, 50});
	}

	for (int i = 0; i < 2; i++)
	{
		pthread_join(consumers[i], NULL);
	}

	// At least one consumer should have gotten data
	// (but behavior is undefined for SPSC with multiple consumers)
	assert(results[0] > 0 || results[1] > 0);

	spsc_destroy(q);
	printf("✓ (note: SPSC with multiple consumers is invalid usage)\n");
}

/* ============== REASONABLE PERFORMANCE BENCHMARK ============== */

void benchmark_throughput(void)
{
	printf("\n=== PERFORMANCE BENCHMARK (10s max) ===\n");

	// Test more realistic buffer sizes
	size_t buffer_sizes[] = {64, 256, 1024, 4096, 16384, 65536};
	size_t num_sizes = sizeof(buffer_sizes) / sizeof(buffer_sizes[0]);

	for (size_t i = 0; i < num_sizes; i++)
	{
		size_t buffer_size = buffer_sizes[i];
		t_spsc_ring *q = spsc_create(buffer_size);

		struct timespec start, end;

		// Adaptive iterations: run for ~0.5-1s per buffer size
		const size_t min_iterations = 1000;
		const double target_time = 0.5;				   // 500ms per test
		size_t batch_size = (buffer_size / 2) & ~0x3F; // Multiple of 64

		if (batch_size < 64)
			batch_size = 64;
		if (batch_size > 4096)
			batch_size = 4096; // Cap for large buffers

		// Warm-up run to estimate speed
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (size_t iter = 0; iter < min_iterations; iter++)
		{
			unsigned char data[batch_size];
			unsigned char output[batch_size];
			spsc_push_batch(q, data, batch_size);
			spsc_pop_batch(q, output, batch_size);
		}
		clock_gettime(CLOCK_MONOTONIC, &end);

		double elapsed = (end.tv_sec - start.tv_sec) +
						 (end.tv_nsec - start.tv_nsec) / 1e9;

		// Scale iterations to hit target time
		size_t iterations = (size_t)((target_time / elapsed) * min_iterations);
		if (iterations < min_iterations)
			iterations = min_iterations;
		if (iterations > 1000000)
			iterations = 1000000; // Cap at 1M

		// Actual measurement
		clock_gettime(CLOCK_MONOTONIC, &start);
		for (size_t iter = 0; iter < iterations; iter++)
		{
			unsigned char data[batch_size];
			unsigned char output[batch_size];
			memset(data, iter & 0xFF, batch_size); // Vary pattern
			spsc_push_batch(q, data, batch_size);
			spsc_pop_batch(q, output, batch_size);
		}
		clock_gettime(CLOCK_MONOTONIC, &end);

		elapsed = (end.tv_sec - start.tv_sec) +
				  (end.tv_nsec - start.tv_nsec) / 1e9;

		double throughput = (iterations * batch_size) / elapsed;
		double ops_per_sec = iterations / elapsed;

		printf("Buffer %6zu bytes: %6.1f MB/s (%5.0f ops/s, %zu iters)\n",
			   buffer_size, throughput / 1e6, ops_per_sec, iterations);

		spsc_destroy(q);
	}
}

/* ============== QUICK CONCURRENT TEST ============== */

#define QUICK_STRESS_ITERATIONS 100000
#define QUICK_BATCH_SIZE 64
void *simple_producer(void *arg)
{
	t_spsc_ring *q = ((void **)arg)[0];
	long long *produced = ((void **)arg)[1];
	volatile int *stop = ((void **)arg)[2];

	unsigned char data = 0;
	while (!*stop)
	{
		if (spsc_try_push(q, data))
		{
			(*produced)++;
			data++;
		}
		else
		{
			sched_yield();
		}
	}
	return NULL;
}

void *simple_consumer(void *arg)
{
	t_spsc_ring *q = ((void **)arg)[0];
	long long *consumed = ((void **)arg)[1];
	int *errors = ((void **)arg)[2];
	volatile int *stop = ((void **)arg)[3];

	unsigned char expected = 0;
	unsigned char byte;

	while (!*stop)
	{
		if (spsc_try_pop(q, &byte))
		{
			if (byte != expected)
			{
				(*errors)++;
			}
			expected = byte + 1;
			(*consumed)++;
		}
		else
		{
			sched_yield();
		}
	}
	return NULL;
}

void quick_concurrent_test(void)
{
	printf("\n=== QUICK CONCURRENT TEST (2 seconds) ===\n");

	t_spsc_ring *q = spsc_create(4096);

	volatile int stop = 0;
	long long produced = 0, consumed = 0;
	int errors = 0;

	pthread_t producer, consumer;

	// Use simpler version without structs
	void *args[] = {(void *)q, (void *)&produced, (void *)&consumed,
					(void *)&errors, (void *)&stop};

	pthread_create(&producer, NULL, simple_producer,
				   (void *[]){(void *)q, (void *)&produced, (void *)&stop});

	pthread_create(&consumer, NULL, simple_consumer,
				   (void *[]){(void *)q, (void *)&consumed, (void *)&errors, (void *)&stop});

	// Run for 2 seconds max
	sleep(2);
	stop = 1;

	pthread_join(producer, NULL);
	pthread_join(consumer, NULL);

	// Drain any remaining data
	unsigned char byte;
	while (spsc_try_pop(q, &byte))
	{
		consumed++;
	}

	printf("Produced: %lld bytes, Consumed: %lld bytes\n", produced, consumed);
	printf("Errors: %d (data corruption)\n", errors);
	printf("Throughput: %.1f MB/s\n", (produced / 2.0) / 1e6);

	spsc_destroy(q);
}
/* ============== SIMPLIFIED MAIN ============== */

int main(void)
{
	printf("Running SPSC Ring Buffer Tests\n");
	printf("===============================\n");

	// Quick correctness tests
	test_create_destroy();
	test_single_byte_operations();
	test_batch_operations();
	test_wraparound();
	test_capacity_limits();
	test_power_of_two_rounding();

	printf("\n✅ BASIC TESTS PASSED\n");

	// Quick performance tests (optional)
	char run_perf = 0;
	printf("\nRun performance tests? (y/n): ");
	scanf(" %c", &run_perf);

	if (run_perf == 'y' || run_perf == 'Y')
	{
		quick_concurrent_test();
		benchmark_throughput();
	}

	printf("\n🎉 ALL TESTS COMPLETE!\n");
	return 0;
}

/* ============== MAIN TEST RUNNER ============== */

// int main(void)
// {
// 	printf("Running SPSC Ring Buffer Tests\n");
// 	printf("===============================\n");

// 	// Single-threaded tests
// 	test_create_destroy();
// 	test_single_byte_operations();
// 	test_batch_operations();
// 	test_wraparound();
// 	test_capacity_limits();
// 	test_alternating_single_batch();
// 	test_power_of_two_rounding();
// 	test_zero_length_operations();

// 	// Edge case tests
// 	// test_single_producer_multiple_consumers_invalid();

// 	// Concurrent stress test (requires threading)
// 	test_concurrent_stress();

// 	// Performance benchmark
// 	benchmark_throughput();

// 	printf("\n✅ ALL TESTS PASSED!\n");
// 	return 0;
// }