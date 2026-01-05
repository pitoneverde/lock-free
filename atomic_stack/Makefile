CC=cc -std=c11
CFLAGS=-O2 -mcx16 -Iinclude
LFLAGS=-latomic -lpthread

BASE_SRC=atomic_stack.c
BASE_TEST_SRC=atomic_stack_tests.c
STRESS_TEST_SRC=atomic_stack_stress.c
HP_SRC=atomic_stack_hp.c hazard_pointers.c

NAME=test_atomic_stack
STRESS_NAME=stress_test_atomic_stack
HP_NAME=test_atomic_stack_hp
STRESS_HP_NAME=stress_test_atomic_stack_hp

all: $(NAME) $(STRESS_NAME) $(HP_NAME) $(STRESS_HP_NAME)

$(NAME): $(BASE_SRC) $(BASE_TEST_SRC)
	$(CC) $(CFLAGS) -DBASE_STACK $(BASE_SRC) $(BASE_TEST_SRC) $(LFLAGS) -o $@

$(STRESS_NAME): $(BASE_SRC) $(STRESS_TEST_SRC)
	$(CC) $(CFLAGS) -DBASE_STACK $(BASE_SRC) $(STRESS_TEST_SRC) $(LFLAGS) -o $@

$(HP_NAME): $(HP_SRC) $(BASE_TEST_SRC)
	$(CC) $(CFLAGS) -DHP_STACK $(HP_SRC) $(BASE_TEST_SRC) $(LFLAGS) -o $@

$(STRESS_HP_NAME): $(HP_SRC) $(STRESS_TEST_SRC)
	$(CC) $(CFLAGS) -DHP_STACK $(HP_SRC) $(STRESS_TEST_SRC) $(LFLAGS) -o $@

# Run all tests
test: all
	@echo "=== Testing Basic Atomic Stack ==="
	@./$(NAME) || echo "Basic test failed"
	@echo ""
	@./$(STRESS_NAME) || echo "Basic stress test failed"
	@echo ""
	@echo "=== Testing Hazard Pointer Atomic Stack ==="
	@./$(HP_NAME) || echo "Hazard Pointer test failed"
	@echo ""
	@./$(STRESS_HP_NAME) || echo "Hazard Pointer stress test failed"

valgrind: valgrind-base valgrind-hp-base

valgrind-base:
	valgrind --leak-check=full --show-leak-kinds=all ./$(NAME)

valgrind-hp:
	valgrind --leak-check=full --show-leak-kinds=all ./$(HP_NAME)

clean:
	rm -rf *.o

fclean: clean
	rm -rf $(NAME) $(STRESS_NAME) $(HP_NAME) $(STRESS_HP_NAME)

.PHONY: all test valgrind valgrind-base valgrind-hp-base clean fclean