# Compiler and flags
CC = gcc
CFLAGS = -g -O3 -march=native -Wall -Wextra -Iinclude
SANITIZERS = -fsanitize=address -fsanitize=undefined
THREAD_FLAGS = -pthread
RCU_FLAGS = -lurcu

# Source files
BASIC_SRC = ht.c
RWLOCK_SRC = rw_ht.c
RCU_SRC = rcu_ht.c
SINGLE_TEST_SRC = test_hashtable.c
CONCURRENT_TEST_SRC = test_concurrent.c

# Targets
BASIC_NAME = test_basic
RWLOCK_NAME = test_rwlock
RCU_NAME = test_rcu
RWLOCK_CONCURRENT_NAME = test_rwlock_concurrent
RCU_CONCURRENT_NAME = test_rcu_concurrent

all: $(BASIC_NAME) $(RWLOCK_NAME) $(RCU_NAME) $(RWLOCK_CONCURRENT_NAME) $(RCU_CONCURRENT_NAME)

$(BASIC_NAME): $(SINGLE_TEST_SRC) $(BASIC_SRC)
	$(CC) $(CFLAGS) -DBASIC_HASHTABLE $(SINGLE_TEST_SRC) $(BASIC_SRC) -o $@

$(RWLOCK_NAME): $(SINGLE_TEST_SRC) $(RWLOCK_SRC)
	$(CC) $(CFLAGS) -DRWLOCK_HASHTABLE $(SINGLE_TEST_SRC) $(RWLOCK_SRC) $(THREAD_FLAGS) -o $@

$(RCU_NAME): $(SINGLE_TEST_SRC) $(RCU_SRC)
	$(CC) $(CFLAGS) -DRCU_HASHTABLE $(SINGLE_TEST_SRC) $(RCU_SRC) $(THREAD_FLAGS) $(RCU_FLAGS) -o $@

$(RWLOCK_CONCURRENT_NAME): $(CONCURRENT_TEST_SRC) $(RWLOCK_SRC)
	$(CC) $(CFLAGS) -DRWLOCK_HASHTABLE $(CONCURRENT_TEST_SRC) $(RWLOCK_SRC) $(THREAD_FLAGS) -o $@

$(RCU_CONCURRENT_NAME): $(CONCURRENT_TEST_SRC) $(RCU_SRC)
	$(CC) $(CFLAGS) -DRCU_HASHTABLE $(CONCURRENT_TEST_SRC) $(RCU_SRC) $(THREAD_FLAGS) $(RCU_FLAGS) -o $@

# Run all tests
test: all
	@echo "=== Testing Basic Hashtable ==="
	@./$(BASIC_NAME) || echo "Basic test failed"
	@echo ""
	@echo "=== Testing RWLock Hashtable (single-threaded) ==="
	@./$(RWLOCK_NAME) || echo "RWLock single-threaded test failed"
	@echo ""
	@echo "=== Testing RCU Hashtable (single-threaded) ==="
	@./$(RCU_NAME) || echo "RCU single-threaded test failed"
	@echo ""
	@echo "=== Testing RWLock Hashtable (concurrent) ==="
	@./$(RWLOCK_CONCURRENT_NAME) || echo "RWLock concurrent test failed"
	@echo ""
	@echo "=== Testing RCU Hashtable (concurrent) ==="
	@./$(RCU_CONCURRENT_NAME) || echo "RCU concurrent test failed"

# Valgrind targets
valgrind-basic: $(BASIC_NAME)
	valgrind --leak-check=full --show-leak-kinds=all ./$(BASIC_NAME)

valgrind-rwlock: $(RWLOCK_NAME)
	valgrind --leak-check=full --show-leak-kinds=all ./$(RWLOCK_NAME)

valgrind-rcu: $(RCU_NAME)
	valgrind --leak-check=full --show-leak-kinds=all ./$(RCU_NAME)

valgrind-rwlock-concurrent: $(RWLOCK_CONCURRENT_NAME)
	valgrind --leak-check=full --show-leak-kinds=all ./$(RWLOCK_CONCURRENT_NAME)

valgrind-rcu-concurrent: $(RCU_CONCURRENT_NAME)
	valgrind --leak-check=full --show-leak-kinds=all ./$(RCU_CONCURRENT_NAME)

# Sanitizer targets
sanitize-basic: $(SINGLE_TEST_SRC) $(BASIC_SRC)
	$(CC) $(CFLAGS) $(SANITIZERS) -DBASIC_HASHTABLE $(SINGLE_TEST_SRC) $(BASIC_SRC) -o sanitize_basic
	./sanitize_basic

sanitize-rwlock: $(SINGLE_TEST_SRC) $(RWLOCK_SRC)
	$(CC) $(CFLAGS) $(SANITIZERS) -DRWLOCK_HASHTABLE $(SINGLE_TEST_SRC) $(RWLOCK_SRC) $(THREAD_FLAGS) -o sanitize_rwlock
	./sanitize_rwlock

sanitize-rcu: $(SINGLE_TEST_SRC) $(RCU_SRC)
	$(CC) $(CFLAGS) $(SANITIZERS) -DRCU_HASHTABLE $(SINGLE_TEST_SRC) $(RCU_SRC) $(THREAD_FLAGS) $(RCU_FLAGS) -o sanitize_rcu
	./sanitize_rcu

clean:
	rm -f *.o $(BASIC_NAME) $(RWLOCK_NAME) $(RCU_NAME) $(RWLOCK_CONCURRENT_NAME) $(RCU_CONCURRENT_NAME) sanitize_*

.PHONY: all test clean valgrind-basic valgrind-rwlock valgrind-rcu valgrind-rwlock-concurrent valgrind-rcu-concurrent sanitize-basic sanitize-rwlock sanitize-rcu