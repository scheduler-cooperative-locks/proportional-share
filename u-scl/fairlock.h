// GCC-VERSION - Not tested for -O3 flag for GCC version above 5.

#ifndef __FAIRLOCK_H__
#define __FAIRLOCK_H__

#define _GNU_SOURCE
#include <stddef.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <linux/futex.h>
#include <pthread.h>
#include "rdtsc.h"
#include "common.h"

typedef unsigned long long ull;

#ifdef DEBUG
typedef struct stats {
    ull reenter;
    ull banned_time;
    ull start;
    ull next_runnable_wait;
    ull prev_slice_wait;
    ull own_slice_wait;
    ull runnable_wait;
    ull succ_wait;
    ull release_succ_wait;
} stats_t;
#endif

typedef struct flthread_info {
    ull banned_until;
    ull weight;
    ull slice;
    ull start_ticks;
    int banned;
#ifdef DEBUG
    stats_t stat;
#endif
} flthread_info_t;

enum qnode_state {
    INIT = 0, // not waiting or after next runnable node
    NEXT,
    RUNNABLE,
    RUNNING
};

typedef struct qnode {
    int state __attribute__ ((aligned (CACHELINE)));
    struct qnode *next __attribute__ ((aligned (CACHELINE)));
} qnode_t __attribute__ ((aligned (CACHELINE)));

typedef struct fairlock {
    qnode_t *qtail __attribute__ ((aligned (CACHELINE)));
    qnode_t *qnext __attribute__ ((aligned (CACHELINE)));
    ull slice __attribute__ ((aligned (CACHELINE)));
    int slice_valid __attribute__ ((aligned (CACHELINE)));
    pthread_key_t flthread_info_key;
    ull total_weight;
} fairlock_t __attribute__ ((aligned (CACHELINE)));

static inline qnode_t *flqnode(fairlock_t *lock) {
    return (qnode_t *) ((char *) &lock->qnext - offsetof(qnode_t, next));
}

static inline int futex(int *uaddr, int futex_op, int val, const struct timespec *timeout) {
    return syscall(SYS_futex, uaddr, futex_op, val, timeout, NULL, 0);
}

int fairlock_init(fairlock_t *lock) {
    int rc;

    lock->qtail = NULL;
    lock->qnext = NULL;
    lock->total_weight = 0;
    lock->slice = 0;
    lock->slice_valid = 0;
    if (0 != (rc = pthread_key_create(&lock->flthread_info_key, NULL))) {
        return rc;
    }
    return 0;
}

static flthread_info_t *flthread_info_create(fairlock_t *lock, int weight) {
    flthread_info_t *info;
    info = malloc(sizeof(flthread_info_t));
    info->banned_until = rdtsc();
    if (weight == 0) {
        int prio = getpriority(PRIO_PROCESS, 0);
        weight = prio_to_weight[prio+20];
    }
    info->weight = weight;
    __sync_add_and_fetch(&lock->total_weight, weight);
    info->banned = 0;
    info->slice = 0;
    info->start_ticks = 0;
#ifdef DEBUG
    memset(&info->stat, 0, sizeof(stats_t));
    info->stat.start = info->banned_until;
#endif
    return info;
}

void fairlock_thread_init(fairlock_t *lock, int weight) {
    flthread_info_t *info;
    info = (flthread_info_t *) pthread_getspecific(lock->flthread_info_key);
    if (NULL != info) {
        free(info);
    }
    info = flthread_info_create(lock, weight);
    pthread_setspecific(lock->flthread_info_key, info);
}

int fairlock_destroy(fairlock_t *lock) {
    //return pthread_key_delete(lock->flthread_info_key);
    return 0;
}

void fairlock_acquire(fairlock_t *lock) {
    flthread_info_t *info;
    ull now;

    info = (flthread_info_t *) pthread_getspecific(lock->flthread_info_key);
    if (NULL == info) {
        info = flthread_info_create(lock, 0);
        pthread_setspecific(lock->flthread_info_key, info);
    }

    if (readvol(lock->slice_valid)) {
        ull curr_slice = lock->slice;
        // If owner of current slice, try to reenter at the beginning of the queue
        if (curr_slice == info->slice && (now = rdtsc()) < curr_slice) {
            qnode_t *succ = readvol(lock->qnext);
            if (NULL == succ) {
                if (__sync_bool_compare_and_swap(&lock->qtail, NULL, flqnode(lock)))
                    goto reenter;
                spin_then_yield(SPIN_LIMIT, (now = rdtsc()) < curr_slice && NULL == (succ = readvol(lock->qnext)));
#ifdef DEBUG
                info->stat.own_slice_wait += rdtsc() - now;
#endif
                // let the succ invalidate the slice, and don't need to wake it up because slice expires naturally
                if (now >= curr_slice)
                    goto begin;
            }
            // if state < RUNNABLE, it won't become RUNNABLE unless someone releases lock,
            // but as no one is holding the lock, there is no race
            if (succ->state < RUNNABLE || __sync_bool_compare_and_swap(&succ->state, RUNNABLE, NEXT)) {
reenter:
#ifdef DEBUG
                info->stat.reenter++;
#endif
                info->start_ticks = now;
                return;
            }
        }
    }
begin:

    if (info->banned) {
        if ((now = rdtsc()) < info->banned_until) {
            ull banned_time = info->banned_until - now;
#ifdef DEBUG
            info->stat.banned_time += banned_time;
#endif
            // sleep with granularity of SLEEP_GRANULARITY us
            while (banned_time > CYCLE_PER_US * SLEEP_GRANULARITY) {
                struct timespec req = {
                    .tv_sec = banned_time / CYCLE_PER_S,
                    .tv_nsec = (banned_time % CYCLE_PER_S / CYCLE_PER_US / SLEEP_GRANULARITY) * SLEEP_GRANULARITY * 1000,
                };
                nanosleep(&req, NULL);
                if ((now = rdtsc()) >= info->banned_until)
                    break;
                banned_time = info->banned_until - now;
            }
            // spin for the remaining (<SLEEP_GRANULARITY us)
            spin_then_yield(SPIN_LIMIT, (now = rdtsc()) < info->banned_until);
        }
    }

    qnode_t n = { 0 };
    while (1) {
        qnode_t *prev = readvol(lock->qtail);
        if (__sync_bool_compare_and_swap(&lock->qtail, prev, &n)) {
            // enter the lock queue
            if (NULL == prev) {
                n.state = RUNNABLE;
                lock->qnext = &n;
            } else {
                if (prev == flqnode(lock)) {
                    n.state = NEXT;
                    prev->next = &n;
                } else {
                    prev->next = &n;
                    // wait until we become the next runnable
#ifdef DEBUG
                    now = rdtsc();
#endif
                    do {
                        futex(&n.state, FUTEX_WAIT_PRIVATE, INIT, NULL);
                    } while (INIT == readvol(n.state));
#ifdef DEBUG
                    info->stat.next_runnable_wait += rdtsc() - now;
#endif
                }
            }
            // invariant: n.state >= NEXT

            // wait until the current slice expires
            int slice_valid;
            ull curr_slice;
            while ((slice_valid = readvol(lock->slice_valid)) && (now = rdtsc()) + SLEEP_GRANULARITY < (curr_slice = readvol(lock->slice))) {
                ull slice_left = curr_slice - now;
                struct timespec timeout = {
                    .tv_sec = 0, // slice will be less then 1 sec
                    .tv_nsec = (slice_left / (CYCLE_PER_US * SLEEP_GRANULARITY)) * SLEEP_GRANULARITY * 1000,
                };
                futex(&lock->slice_valid, FUTEX_WAIT_PRIVATE, 0, &timeout);
#ifdef DEBUG
                info->stat.prev_slice_wait += rdtsc() - now;
#endif
            }
            if (slice_valid) {
                spin_then_yield(SPIN_LIMIT, (slice_valid = readvol(lock->slice_valid)) && rdtsc() < readvol(lock->slice));
                if (slice_valid)
                    lock->slice_valid = 0;
            }
            // invariant: rdtsc() >= curr_slice && lock->slice_valid == 0

#ifdef DEBUG
            now = rdtsc();
#endif
            // spin until RUNNABLE and try to grab the lock
            spin_then_yield(SPIN_LIMIT, RUNNABLE != readvol(n.state) || 0 == __sync_bool_compare_and_swap(&n.state, RUNNABLE, RUNNING));
            // invariant: n.state == RUNNING
#ifdef DEBUG
            info->stat.runnable_wait += rdtsc() - now;
#endif

            // record the successor in the lock so we can notify it when we release
            qnode_t *succ = readvol(n.next);
            if (NULL == succ) {
                lock->qnext = NULL;
                if (0 == __sync_bool_compare_and_swap(&lock->qtail, &n, flqnode(lock))) {
                    spin_then_yield(SPIN_LIMIT, NULL == (succ = readvol(n.next)));
#ifdef DEBUG
                    info->stat.succ_wait += rdtsc() - now;
#endif
                    lock->qnext = succ;
                }
            } else {
                lock->qnext = succ;
            }
            // invariant: NULL == succ <=> lock->qtail == flqnode(lock)

            now = rdtsc();
            info->start_ticks = now;
            info->slice = now + FAIRLOCK_GRANULARITY;
            lock->slice = info->slice;
            lock->slice_valid = 1;
            // wake up successor if necessary
            if (succ) {
                succ->state = NEXT;
                futex(&succ->state, FUTEX_WAKE_PRIVATE, 1, NULL);
            }
            return;
        }
    }
}

void fairlock_release(fairlock_t *lock) {
    ull now, cs;
#ifdef DEBUG
    ull succ_start = 0, succ_end = 0;
#endif
    flthread_info_t *info;

    qnode_t *succ = lock->qnext;
    if (NULL == succ) {
        if (__sync_bool_compare_and_swap(&lock->qtail, flqnode(lock), NULL))
            goto accounting;
#ifdef DEBUG
        succ_start = rdtsc();
#endif
        spin_then_yield(SPIN_LIMIT, NULL == (succ = readvol(lock->qnext)));
#ifdef DEBUG
        succ_end = rdtsc();
#endif
    }
    succ->state = RUNNABLE;

accounting:
    // invariant: NULL == succ || succ->state = RUNNABLE
    info = (flthread_info_t *) pthread_getspecific(lock->flthread_info_key);
    now = rdtsc();
    cs = now - info->start_ticks;
    info->banned_until += cs * (__atomic_load_n(&lock->total_weight, __ATOMIC_RELAXED) / info->weight);
    info->banned = now < info->banned_until;

    if (info->banned) {
        if (__sync_bool_compare_and_swap(&lock->slice_valid, 1, 0)) {
            futex(&lock->slice_valid, FUTEX_WAKE_PRIVATE, 1, NULL);
        }
    }
#ifdef DEBUG
    info->stat.release_succ_wait += succ_end - succ_start;
#endif
}

#endif // __FAIRLOCK_H__
