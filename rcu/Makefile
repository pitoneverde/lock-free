CC = gcc
CFLAGS = -g -O3 -march=native -Wall -Wextra
SANITIZERS = -fsanitize=address -fsanitize=undefined

all: test

rcu_ht.o: rcu_ht.c ht.h
	$(CC) $(CFLAGS) -c rcu_ht.c -lurcu -pthread -o rcu_ht.o

test: test_hashtable.c test_concurrent.c rcu_ht.o
	$(CC) $(CFLAGS) rcu_ht.o test_hashtable.c test_concurrent.c -lurcu -pthread -o test_hashtable
	./test_hashtable

valgrind: test_hashtable
	valgrind --leak-check=full --show-leak-kinds=all ./test_hashtable

sanitize: test_hashtable.c test_concurrent.c rcu_ht.c
	$(CC) $(CFLAGS) $(SANITIZERS) rcu_ht.c test_hashtable.c test_concurrent.c -lurcu -pthread -o test_sanitize
	./test_sanitize

clean:
	rm -f *.o test_hashtable test_sanitize

.PHONY: all test valgrind sanitize clean