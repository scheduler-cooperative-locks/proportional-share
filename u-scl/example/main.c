#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <inttypes.h>
#define gettid() syscall(SYS_gettid)
#include "rdtsc.h"
#include "lock.h"

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in the Makefile or elsewhere
#endif

typedef unsigned long long ull;
typedef struct {
    volatile int *stop;
    pthread_t thread;
    int priority;
#ifdef FAIRLOCK
    int weight;
#endif
    int id;
    double cs;
    double non_cs;
    int ncpu;
    // outputs
    ull loop_in_cs;
    ull lock_acquires;
    ull lock_hold;
    ull time_non_cs;
} task_t __attribute__ ((aligned (64)));

lock_t lock;

void *worker(void *arg) {
    int ret;
    task_t *task = (task_t *) arg;

    if (task->ncpu != 0) {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        for (int i = 0; i < task->ncpu; i++) {
            if (i < 8 || i >= 24)
                CPU_SET(i, &cpuset);
            else if (i < 16)
                CPU_SET(i+8, &cpuset);
            else
                CPU_SET(i-8, &cpuset);
        }
        ret = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        if (ret != 0) {
            perror("pthread_set_affinity_np");
            exit(-1);
        }
    }

    pid_t tid = gettid();
    ret = setpriority(PRIO_PROCESS, tid, task->priority);
    if (ret != 0) {
        perror("setpriority");
        exit(-1);
    }

#ifdef FAIRLOCK
    fairlock_thread_init(&lock, task->weight);
#endif

    // loop
    ull now, start, then;
    ull lock_acquires = 0;
    ull lock_hold = 0;
    ull time_non_cs = 0;
    ull loop_in_cs = 0;
    ull loop_in_non_cs = 0;
    const ull cs_delta = CYCLE_PER_US * task->cs;
    const ull non_cs_delta = CYCLE_PER_US * task->non_cs;
    while (!*task->stop) {

        lock_acquire(&lock);
        now = rdtscp();

        lock_acquires++;
        start = now;
        then = now + cs_delta;

        do {
            loop_in_cs++;
        } while ((now = rdtscp()) < then);

        lock_hold += now - start;

        lock_release(&lock);

        now = rdtscp();
        start = now;
        then = now + non_cs_delta;
        do {
            loop_in_non_cs++;
        } while ((now = rdtscp()) < then);
        time_non_cs += now - start;
    }
    task->lock_acquires = lock_acquires;
    task->loop_in_cs = loop_in_cs;
    task->lock_hold = lock_hold;
    task->time_non_cs = time_non_cs;

    pid_t pid = getpid();
    char path[256];
    char buffer[1024] = { 0 };
    snprintf(path, 256, "/proc/%d/task/%d/schedstat", pid, tid);
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        exit(-1);
    }
    if (read(fd, buffer, 1024) <= 0) {
        perror("read");
        exit(-1);
    }

    printf("id %02d "
            "loop %10llu "
            "lock_acquires %8llu "
            "lock_hold(ms) %10.3f "
            "time_in_non_cs(ms) %10.3f "
            "schedstat %s",
            task->id,
            task->loop_in_cs,
            task->lock_acquires,
            task->lock_hold / (float) (CYCLE_PER_US * 1000),
            task->time_non_cs / (float) (CYCLE_PER_US * 1000),
            buffer);
#if defined(FAIRLOCK) && defined(DEBUG)
    flthread_info_t *info = pthread_getspecific(lock.flthread_info_key);
    printf("  slice %llu\n"
            "  own_slice_wait %llu\n"
            "  prev_slice_wait %llu\n"
            "  runnable_wait %llu\n"
            "  next_runnable_wait %llu\n"
            "  succ_wait %llu\n"
            "  reenter %llu\n"
            "  banned(actual) %llu\n"
            "  banned %llu\n"
            "  elapse %llu\n",
            task->lock_acquires - info->stat.reenter,
            info->stat.own_slice_wait,
            info->stat.prev_slice_wait,
            info->stat.runnable_wait,
            info->stat.next_runnable_wait,
            info->stat.succ_wait,
            info->stat.reenter,
            info->stat.banned_time,
            info->banned_until-info->stat.start,
            info->start_ticks-info->stat.start);
#endif
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("usage: %s <nthreads> <duration> <<cs non-cs prio> <..n>> [NCPU]\n", argv[0]);
	printf("nthreads - no. of threads to be used for experimentation\n");
	printf("duration - the duration of the experiment\n");
	printf("cs - critical section size in us(microseconds)\n");
	printf("non-cs - non-critical section size in us(microseconds)\n");
	printf("prio - priority of the thread\n");
	printf("NCPU - no. of CPUs to be used for the experimentation\n");
        return 1;
    }
    int nthreads = atoi(argv[1]);
    int duration = atoi(argv[2]);
    task_t *tasks = malloc(sizeof(task_t) * nthreads);
    if (argc < 3+nthreads*3) {
        printf("usage: %s <nthreads> <duration> <<cs prio> <..n>> [NCPU]\n", argv[0]);
        return 1;
    }

    int stop __attribute__((aligned (64))) = 0;
#ifdef FAIRLOCK
    int tot_weight = 0;
#endif
    int ncpu = argc > 3 + nthreads*3 ? atoi(argv[3+nthreads*3]) : 0;
    for (int i = 0; i < nthreads; i++) {
        tasks[i].stop = &stop;
        tasks[i].cs = atof(argv[3+i*3]);
        tasks[i].non_cs = atof(argv[4+i*3]);

        tasks[i].priority = atoi(argv[5+i*3]);
#ifdef FAIRLOCK
        int weight = prio_to_weight[priority+20];
        tasks[i].weight = weight;
        tot_weight += weight;
#endif

        tasks[i].ncpu = ncpu;
        tasks[i].id = i;

        tasks[i].loop_in_cs = 0;
        tasks[i].lock_acquires = 0;
        tasks[i].lock_hold = 0;
        tasks[i].time_non_cs = 0;
    }

//#ifdef FAIRLOCK
//    lock_init(&lock, tot_weight);
//#else
    lock_init(&lock);
//#endif

    for (int i = 0; i < nthreads; i++) {
        pthread_create(&tasks[i].thread, NULL, worker, &tasks[i]);
    }
    sleep(duration);
    stop = 1;
    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }
    return 0;
}

