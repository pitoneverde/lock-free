CC = gcc
CFLAGS = -O3 -march=native -Wall -Wextra
SANITIZERS = -fsanitize=address -fsanitize=undefined

all: test

ht.o: ht.c ht.h
	$(CC) $(CFLAGS) -c ht.c -o ht.o

test: test_hashtable.c ht.o
	$(CC) $(CFLAGS) ht.o test_hashtable.c -o test_hashtable
	./test_hashtable

valgrind: test_hashtable
	valgrind --leak-check=full --show-leak-kinds=all ./test_hashtable

sanitize: test_hashtable.c ht.c
	$(CC) $(CFLAGS) $(SANITIZERS) ht.c test_hashtable.c -o test_sanitize
	./test_sanitize

clean:
	rm -f *.o test_hashtable test_sanitize

.PHONY: all test valgrind sanitize clean