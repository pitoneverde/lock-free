CC = gcc
CFLAGS = -std=c11 -O3 -march=native -pthread
TARGET = spsc_test

all: $(TARGET)

$(TARGET): spsc_ring.c spsc_ring.h test_spsc.c
	$(CC) $(CFLAGS) -o $(TARGET) spsc_ring.c test_spsc.c

test: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -g -fsanitize=thread -fsanitize=address
debug: $(TARGET)

clean:
	rm -f $(TARGET) *.o

.PHONY: all test debug clean