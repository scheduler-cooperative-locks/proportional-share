#include <stdio.h>
#include "rdtsc.h"
#include <time.h>

#define WA_FLAG 1
#define RC_INC 2
#define READ_RATIO 1L
#define WRITE_RATIO 9L
#define TOTAL_SLICE (CYCLE_PER_MS * 2L)
#define READ_SLICE_SIZE (TOTAL_SLICE * READ_RATIO)
#define WRITE_SLICE_SIZE (TOTAL_SLICE * WRITE_RATIO)
#define NUMA_NODES 2

#define SPIN_CUTOFF CYCLE_PER_US * 100

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in Makefile or elsewhere
#endif
#define CYCLE_PER_MS (CYCLE_PER_US * 1000L)
#define CYCLE_PER_S (CYCLE_PER_MS * 1000L)

#define readvol(lvalue) (*(volatile typeof(lvalue)*)(&lvalue))

typedef unsigned long long ull;

typedef struct numa_counter {
	unsigned int count;
	char padding[60];
} numa_counter_t;

typedef struct rwlock {
	unsigned long long slice;
	unsigned long long read_slice;
	unsigned long long write_slice;
	char padding1[40];
	numa_counter_t counters[NUMA_NODES];
} rwlock_t;

void rwlock_init(rwlock_t *lock) {
	lock->slice = rdtsc() + READ_SLICE_SIZE;
	lock->read_slice = lock->slice;
	lock->write_slice = 0;
	for (int i = 0; i < NUMA_NODES; i++) {
		lock->counters[i].count = 0;
	}
}

void rwlock_writer_lock(rwlock_t *lock) {
	ull now = 0;
	ull time_diff = 0;
	ull ns = 0;
	struct timespec time_to_sleep;

	while (1) {
		if ((readvol(lock->write_slice) == readvol(lock->slice)) &&
		    ((now = rdtsc()) < lock->slice)) {
			while (!__sync_bool_compare_and_swap(&lock->counters[0].count, 0, WA_FLAG));
			while (!__sync_bool_compare_and_swap(&lock->counters[1].count, 0, WA_FLAG));

			return;
		} else {
			ull curr_slice = readvol(lock->slice);

			now = rdtscp();
			while (now < curr_slice) {
				time_diff = curr_slice - now;
				if (time_diff > SPIN_CUTOFF) {
					ns = time_diff / CYCLE_PER_US / 1000;
					time_to_sleep.tv_sec = ns / 1000000000;
					time_to_sleep.tv_nsec = ns % 1000000000;

					nanosleep(&time_to_sleep, NULL);
				} else {
					//sched_yield();
				}
				now = rdtscp();
			}
			if (__sync_bool_compare_and_swap(&lock->slice, curr_slice, rdtsc() + WRITE_SLICE_SIZE)) {
				lock->write_slice = lock->slice;
			}
		}
	}
}

void rwlock_reader_lock(rwlock_t *lock) {
	ull now = 0;
	ull time_diff = 0;
	ull ns = 0;
	struct timespec time_to_sleep;

	while (1) {
		int chip = 0, core = 0;
		now = rdtscp_(&chip, &core);
		if (( readvol(lock->read_slice) == readvol(lock->slice)) &&
		    (now < lock->slice)) {
			if (core < 8) {
				(void)__sync_fetch_and_add(&lock->counters[0].count, RC_INC);
				while((readvol(lock->counters[0].count) & WA_FLAG) == 1);
			} else if (core < 16) {
				(void)__sync_fetch_and_add(&lock->counters[1].count, RC_INC);
				while((readvol(lock->counters[1].count) & WA_FLAG) == 1);
			}

			return;
		} else {
			ull curr_slice = readvol(lock->slice);

			now = rdtscp();
			while (now < curr_slice) {
				time_diff = curr_slice - now;
				if (time_diff > SPIN_CUTOFF) {
					ns = time_diff / CYCLE_PER_US / 1000;
					time_to_sleep.tv_sec = ns / 1000000000;
					time_to_sleep.tv_nsec = ns % 1000000000;

					nanosleep(&time_to_sleep, NULL);
				} else {
					//sched_yield();
				}
				now = rdtscp();
			}

			if (__sync_bool_compare_and_swap(&lock->slice, curr_slice, rdtsc() + READ_SLICE_SIZE)) {
				lock->read_slice = lock->slice;
			}
		}
	}
}

void rwlock_writer_unlock(rwlock_t *lock) {
	ull curr_slice = readvol(lock->slice);
	ull now = rdtsc();
	if (now > curr_slice) {
		if (__sync_bool_compare_and_swap(&lock->slice, curr_slice, now + READ_SLICE_SIZE)) {
			lock->read_slice = lock->slice;
		}
	}

	(void)__sync_fetch_and_add(&lock->counters[0].count, -WA_FLAG);
	(void)__sync_fetch_and_add(&lock->counters[1].count, -WA_FLAG);

	return;
}

void rwlock_reader_unlock(rwlock_t *lock) {
	int core = 0, chip = 0;
	ull curr_slice = readvol(lock->slice);
	ull now = rdtscp_(&chip, &core);

	if (now > curr_slice) {
		if (__sync_bool_compare_and_swap(&lock->slice, curr_slice, rdtsc() + WRITE_SLICE_SIZE)) {
			lock->write_slice = lock->slice;
		}
	}

	if (core < 8) {
		(void) __sync_fetch_and_add(&lock->counters[0].count, -RC_INC);
	} else if (core < 16) {
		(void) __sync_fetch_and_add(&lock->counters[1].count, -RC_INC);
	}

	return;
}

void rwlock_destroy(rwlock_t *lock) {
	/* Try to prevent the readers and writers from acquiring lock */
	(void)__sync_fetch_and_add(&lock->counters[0].count, RC_INC + WA_FLAG);
	(void)__sync_fetch_and_add(&lock->counters[1].count, RC_INC + WA_FLAG);
	return;
}
