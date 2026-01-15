# Makefile
CC = gcc
CFLAGS = -Wall -Wextra -pthread -O2 -g -D_GNU_SOURCE
TSAN_FLAGS = -fsanitize=thread
BENCH_FLAGS = -lm

MUTEX = simple_mutex.c

TEST = test_simple_mutex
STRESS_TEST = stress_test_simple_mutex
BENCHMARK = benchmark_simple_mutex

TESTS = simple_mutex_tests.c
STRESS_TESTS = simple_mutex_stress.c
BENCHMARKS = simple_mutex_bench.c

all: $(TEST) $(STRESS_TEST)

$(TEST): $(TESTS) $(MUTEX)
	$(CC) $(CFLAGS) -o $@ $^

$(STRESS_TEST): $(STRESS_TESTS) $(MUTEX)
	$(CC) $(CFLAGS) -o $@ $^

$(BENCHMARK)): $(BENCHMARKS) $(MUTEX)
	$(CC) $(CFLAGS) -o $@ $^

test: $(TEST)
	./$(TEST)

stress: $(STRESS_TEST)
	./$(STRESS_TEST)

bench: $(BENCHMARK)
	./$(BENCHMARK)

test-all: test stress bench

test-tsan: $(TESTS) $(MUTEX)
	$(CC) $(CFLAGS) $(TSAN_FLAGS) -o $(TEST)-tsan $(TESTS) $(MUTEX)
	./$(TEST)-tsan

perf-record: $(BENCHMARK_TARGET)
	perf record -g -- ./$(BENCHMARK_TARGET)

perf-report:
	perf report -g graph --no-children

clean:
	rm -f $(TEST) $(TEST)-tsan $(STRESS_TEST) $(BENCHMARK) *.o perf.data*

.PHONY: all test stress test-tsan clean bench perf-report perf-record