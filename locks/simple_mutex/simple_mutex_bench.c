#include "simple_mutex.h"
#include <pthread.h>

void bench_uncontended_latency();
void bench_uncontended_latency_pthread();
void bench_contended_throughput();
void bench_contended_throughput_pthread();
