CC = gcc
CFLAGS = -g -O3 -march=native -Wall -Wextra -pthread -lrt
SANITIZERS = -fsanitize=address -fsanitize=undefined

all: test

rw_ht.o: rw_ht.c ht.h
	$(CC) $(CFLAGS) -c rw_ht.c -o rw_ht.o

test: test_hashtable.c test_concurrent.c rw_ht.o
	$(CC) $(CFLAGS) rw_ht.o test_hashtable.c test_concurrent.c -o test_hashtable
	./test_hashtable

valgrind: test_hashtable
	valgrind --leak-check=full --show-leak-kinds=all ./test_hashtable

sanitize: test_hashtable.c test_concurrent.c rw_ht.c
	$(CC) $(CFLAGS) $(SANITIZERS) rw_ht.c test_hashtable.c test_concurrent.c -o test_sanitize
	./test_sanitize

clean:
	rm -f *.o test_hashtable test_sanitize

.PHONY: all test valgrind sanitize clean