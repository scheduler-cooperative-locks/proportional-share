#include <stdio.h>
#include "rdtsc.h"
#include <time.h>
#include <stdint.h>
#include <common.h>

#define WA_FLAG 1
#define RC_INC 2

#define TOTAL_SLICE (CYCLE_PER_MS * 20L)
#define READ_SLICE_SIZE (TOTAL_SLICE * lock->reader_weight / lock->total_weight)
#define WRITE_SLICE_SIZE (TOTAL_SLICE * lock->writer_weight / lock->total_weight)
#define INIT_SLICE_SIZE CYCLE_PER_US * 100

typedef unsigned long long ull;

/* Per-NUMA-node counter */
typedef struct numa_counter {
	unsigned int count;
	char padding[60];
} numa_counter_t;

/* Lock structure */
typedef struct rwlock {
	ull slice;
	ull read_slice;
	ull write_slice;
	uint32_t reader_weight;
	uint32_t writer_weight;
	uint32_t total_weight;
	char padding1[28];
	numa_counter_t counters[NUMA_NODES];
} rwlock_t;

void rwlock_init(rwlock_t *lock) {
	lock->slice = rdtsc() + INIT_SLICE_SIZE;
	lock->read_slice = lock->slice;
	lock->write_slice = 0;
	for (int i = 0; i < NUMA_NODES; i++) {
		lock->counters[i].count = 0;
	}
	lock->reader_weight = 0;
	lock->writer_weight = 0;
	lock->total_weight = 0;
}

/* Writer lock code */
void rwlock_writer_lock(rwlock_t *lock) {
	ull now = 0;
	ull time_diff = 0;
	ull ns = 0;
	struct timespec time_to_sleep;
	int tot_weight = 0;

	/* 
	 * Identify the priority of the writer thread. We assume that all the
 	 * writers will have the same priority. Only one thread needs to identify the
 	 * priority and set the writer-weight.
	 */
	if(!lock->writer_weight) {
		int prio = getpriority(PRIO_PROCESS, 0);
		int weight = prio_to_weight[prio+20];
		if (__sync_bool_compare_and_swap(&lock->writer_weight, 0, weight)) {
retry:			tot_weight = __atomic_load_n(&lock->total_weight,
											 __ATOMIC_RELAXED);
			if (!__sync_bool_compare_and_swap(&lock->total_weight, tot_weight,
											  weight + tot_weight)) {
				goto retry;
			}
		}
	}

	while (1) {
		if ((readvol(lock->write_slice) == readvol(lock->slice)) &&
		    ((now = rdtsc()) < lock->slice)) {
			/*
			 * If the writer is unable to acquire the lock immediately, sleep
			 * for a while and try again. The idea is to let the owner thread
			 * run so that ir can quickly release the lock.
			 * TODO: Add exponential backoff instead of a fixed SPIN_CUTOFF
			 */

			// All writers need to set the counters for all NUMA nodes. 

			// TODO: Make the CAS generic by looping through all NUMA-nodes.
			while (!__sync_bool_compare_and_swap(&lock->counters[0].count, 0,
												 WA_FLAG)) {
				time_diff = rdtsc() - now;
				if (time_diff > SPIN_CUTOFF) {
					ns = CYCLE_PER_US * 100;
					time_to_sleep.tv_sec = ns / 1000000000;
					time_to_sleep.tv_nsec = ns % 1000000000;

					nanosleep(&time_to_sleep, NULL);
					now = rdtsc();
				}
			}

			while (!__sync_bool_compare_and_swap(&lock->counters[1].count, 0,
												 WA_FLAG)) {
				time_diff = rdtsc() - now;
				if (time_diff > SPIN_CUTOFF) {
					ns = CYCLE_PER_US * 100;
					time_to_sleep.tv_sec = ns / 1000000000;
					time_to_sleep.tv_nsec = ns % 1000000000;

					nanosleep(&time_to_sleep, NULL);
					now = rdtsc();
				}
			}

			return;
		} else {
			// Wait until the writers owns the slice.
			ull curr_slice = readvol(lock->slice);

			now = rdtscp();
			/* 
			 * We know the exact time when the slice will be owned by the
			 * writers. So, sleep until that time if the time diff is more than
			 * SPIN_CUTOFF. Otherwise, just spin.
			 */
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
			// TODO: There is still a chance that total_weight is 0
			// leading to divide-by-zero crash.

			// Turn for the writers to own the slice. If the readers do not
			// switch the slice ownership, better do it yourself.
			if (__sync_bool_compare_and_swap(&lock->slice, curr_slice,
											 rdtsc() + WRITE_SLICE_SIZE)) {
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
	int tot_weight = 0;

	/* 
	 * Identify the priority of the reader thread. We assume that all the
 	 * readers will have the same priority. Only one thread needs to identify the
 	 * priority and set the writer-weight.
	 */
	if(!lock->reader_weight) {
		int prio = getpriority(PRIO_PROCESS, 0);
		int weight = prio_to_weight[prio+20];
		if (__sync_bool_compare_and_swap(&lock->reader_weight, 0, weight)) {
retry:			tot_weight = __atomic_load_n(&lock->total_weight,
											 __ATOMIC_RELAXED);
			if (!__sync_bool_compare_and_swap(&lock->total_weight, tot_weight,
											  weight + tot_weight)) {
				goto retry;
			}
		}
	}

	while (1) {
		int chip = 0, core = 0;
		// Identify the NUMA node where the reader is acquiring the lock and
		// appropriately set that particular NUMA counter.
		now = rdtscp_(&chip, &core);
		if (( readvol(lock->read_slice) == readvol(lock->slice)) &&
		    (now < lock->slice)) {
			// TODO: Make the core check generic.
			if (core < 8) {
				(void)__sync_fetch_and_add(&lock->counters[0].count, RC_INC);

				/*
				 * If the reader is unable to acquire the lock immediately,
				 * sleep for a while and try again. The idea is to let the
				 * owner thread run so that it can quickly release the lock.
				 * TODO: Add exponential backoff instead of a fixed
				 * SPIN_CUTOFF.
				 */
				while((readvol(lock->counters[0].count) & WA_FLAG) == 1) {
					time_diff = rdtsc() - now;
					if (time_diff > SPIN_CUTOFF) {
						ns = CYCLE_PER_US * 100;
						time_to_sleep.tv_sec = ns / 1000000000;
						time_to_sleep.tv_nsec = ns % 1000000000;

						nanosleep(&time_to_sleep, NULL);
						now = rdtsc();
					}
				}
			} else if (core < 16) {
				(void)__sync_fetch_and_add(&lock->counters[1].count, RC_INC);
				while((readvol(lock->counters[1].count) & WA_FLAG) == 1) {
					time_diff = rdtsc() - now;
					if (time_diff > SPIN_CUTOFF) {
						ns = CYCLE_PER_US * 100;
						time_to_sleep.tv_sec = ns / 1000000000;
						time_to_sleep.tv_nsec = ns % 1000000000;

						nanosleep(&time_to_sleep, NULL);
						now = rdtsc();
					}
				}
			}

			return;
		} else {
			// Wait until the readers owns the slice.
			ull curr_slice = readvol(lock->slice);

			now = rdtscp();
			/* 
			 * We know the exact time when the slice will be owned by the
			 * writers. So, sleep until that time if the time diff is more than
			 * SPIN_CUTOFF. Otherwise, just spin.
			 */
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

			// TODO: There is still a chance that total_weight is 0
			// leading to divide-by-zero crash.

			// Turn for the writers to own the slice. If the readers do not
			// switch the slice ownership, better do it yourself.
			if (__sync_bool_compare_and_swap(&lock->slice, curr_slice,
											 rdtsc() + READ_SLICE_SIZE)) {
				lock->read_slice = lock->slice;
			}
		}
	}
}

void rwlock_writer_unlock(rwlock_t *lock) {
	ull curr_slice = readvol(lock->slice);
	ull now = rdtsc();

	// Writer slice has expired. So be kind and do the needful.
	if (now > curr_slice) {
		// TODO: There is still a chance that total_weight is 0
		// leading to divide-by-zero crash.
		if (__sync_bool_compare_and_swap(&lock->slice, curr_slice,
										 now + READ_SLICE_SIZE)) {
			lock->read_slice = lock->slice;
		}
	}

	// Clean the writer flags for all NUMA counters.
	(void)__sync_fetch_and_add(&lock->counters[0].count, -WA_FLAG);
	(void)__sync_fetch_and_add(&lock->counters[1].count, -WA_FLAG);

	return;
}

void rwlock_reader_unlock(rwlock_t *lock) {
	int core = 0, chip = 0;
	ull curr_slice = readvol(lock->slice);
	ull now = rdtscp_(&chip, &core);

	// Reader slice has expired. So be kind and do the needful.
	if (now > curr_slice) {
		// TODO: There is still a chance that total_weight is 0
		// leading to divide-by-zero crash.
		if (__sync_bool_compare_and_swap(&lock->slice, curr_slice,
										 rdtsc() + WRITE_SLICE_SIZE)) {
			lock->write_slice = lock->slice;
		}
	}

	/*
	 * Reduce the counter from where the reader acquired the lock. We assume
	 * that the threads are pinned so they wont change the CPU/NUMA node. If
	 * the threads can change CPU/NUMA nodes, we need to remember the NUMA
	 * counter locally and just decrement that counter.
	 */
	if (core < 8) {
		(void) __sync_fetch_and_add(&lock->counters[0].count, -RC_INC);
	} else if (core < 16) {
		(void) __sync_fetch_and_add(&lock->counters[1].count, -RC_INC);
	}

	return;
}

void rwlock_destroy(rwlock_t *lock) {
	/* Try to prevent the readers and writers from acquiring lock */
	while (!__sync_bool_compare_and_swap(&lock->counters[0].count, 0,
										 RC_INC + WA_FLAG));
	while (!__sync_bool_compare_and_swap(&lock->counters[1].count, 0,
										 RC_INC + WA_FLAG));
	return;
}
