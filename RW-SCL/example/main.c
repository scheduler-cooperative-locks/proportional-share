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

int duration;

typedef unsigned long long ull;
typedef struct {
    volatile int *stop;
    pthread_t thread;
    int priority;
    int id;
    double cs;
    int ncpu;
    // outputs
    ull loop_in_cs;
    ull lock_acquires;
    ull lock_hold;
    ull wait_time;
} task_t __attribute__ ((aligned (64)));

lock_t lock;

void *read_worker(void *arg) {
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

    // loop
    ull now, start, then;
    ull temp;
    ull wait_time = 0;
    ull lock_acquires = 0;
    ull lock_hold = 0;
    ull loop_in_cs = 0;
    const ull delta = CYCLE_PER_US * task->cs;
    while (!*task->stop) {

	temp = rdtscp();
	lock_reader_lock(&lock);
        now = rdtscp();

	wait_time += now - temp;

        lock_acquires++;
        start = now;
        then = now + delta;

        do {
            loop_in_cs++;
        } while ((now = rdtscp()) < then);

        lock_hold += now - start;

	lock_reader_unlock(&lock);
    }
    task->lock_acquires = lock_acquires;
    task->loop_in_cs = loop_in_cs;
    task->lock_hold = lock_hold;
    task->wait_time = wait_time;

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

    printf("(R)id %02d "
            "loop %10llu "
            "lock_acquires %8llu "
	    "wait_time(ms) %10.3f "
            "lock_hold(ms) %10.3f "
	    "other_time(ms) %10.3f "
            "schedstat %s",
            task->id,
            task->loop_in_cs,
            task->lock_acquires,
            task->wait_time / (float) (CYCLE_PER_US * 1000),
            task->lock_hold / (float) (CYCLE_PER_US * 1000),
	    (duration * 1000) - (task->wait_time / (float) (CYCLE_PER_US * 1000) + task->lock_hold / (float) (CYCLE_PER_US * 1000)),
            buffer);
    return 0;
}

void *write_worker(void *arg) {
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

    // loop
    ull now, start, then;
    ull temp;
    ull wait_time = 0;
    ull lock_acquires = 0;
    ull lock_hold = 0;
    ull loop_in_cs = 0;
    const ull delta = CYCLE_PER_US * task->cs;
    while (!*task->stop) {

	temp = rdtscp();
	lock_writer_lock(&lock);
        now = rdtscp();

	wait_time += now - temp;

        lock_acquires++;
        start = now;
        then = now + delta;

        do {
            loop_in_cs++;
        } while ((now = rdtscp()) < then);

        lock_hold += now - start;

	lock_writer_unlock(&lock);
    }
    task->lock_acquires = lock_acquires;
    task->loop_in_cs = loop_in_cs;
    task->lock_hold = lock_hold;
    task->wait_time = wait_time;

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

    printf("(W)id %02d "
            "loop %10llu "
            "lock_acquires %8llu "
	    "wait_time(ms) %10.3f "
            "lock_hold(ms) %10.3f "
	    "other_time(ms) %10.3f "
            "schedstat %s",
            task->id,
            task->loop_in_cs,
            task->lock_acquires,
            task->wait_time / (float) (CYCLE_PER_US * 1000),
            task->lock_hold / (float) (CYCLE_PER_US * 1000),
	    (duration * 1000) - (task->wait_time / (float) (CYCLE_PER_US * 1000) + task->lock_hold / (float) (CYCLE_PER_US * 1000)),
            buffer);
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("usage: %s <read-threads> <write-threads> <duration> <<cs prio> <..n>> [NCPU]\n", argv[0]);
        return 1;
    }
    int nthreads = atoi(argv[1]) + atoi(argv[2]);
    int r_threads = atoi(argv[1]);
    //int w_threads = atoi(argv[2]);
    duration = atoi(argv[3]);
    task_t *tasks = malloc(sizeof(task_t) * nthreads);
    if (argc < 4+nthreads*2) {
        printf("usage: %s <nthreads> <duration> <<cs prio> <..n>> [NCPU]\n", argv[0]);
        return 1;
    }

#ifdef WRITE_PREF
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NONRECURSIVE_NP);
#elif READ_PREF
    pthread_rwlockattr_t attr;
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_READER_NP);
#endif

#ifdef PTHREAD_RW
    lock_init(&lock, &attr);
#elif RWLOCK_SCL
    lock_init(&lock);
#elif RWLOCK_SCL_ORIG
    lock_init(&lock);
#endif

    int stop __attribute__((aligned (64))) = 0;
    int ncpu = argc > 4 + nthreads*2 ? atoi(argv[4+nthreads*2]) : 0;
    for (int i = 0; i < nthreads; i++) {
        tasks[i].stop = &stop;
        tasks[i].cs = atof(argv[4+i*2]);

        int priority = atoi(argv[5+i*2]);
        tasks[i].priority = priority;

        tasks[i].ncpu = ncpu;
        tasks[i].id = i;

        tasks[i].loop_in_cs = 0;
        tasks[i].lock_acquires = 0;
        tasks[i].lock_hold = 0;
    }

    for (int i = 0; i < nthreads; i++) {
	if (i < r_threads) {
	        pthread_create(&tasks[i].thread, NULL, read_worker, &tasks[i]);
	} else {
	        pthread_create(&tasks[i].thread, NULL, write_worker, &tasks[i]);
	}
    }

    sleep(duration);
    stop = 1;
    for (int i = 0; i < nthreads; i++) {
        pthread_join(tasks[i].thread, NULL);
    }

    lock_destroy(&lock);

    return 0;
}
