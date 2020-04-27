#include <sched.h>

#define CACHELINE 64
#ifndef SPIN_LIMIT
#define SPIN_LIMIT 20
#endif
#define SLEEP_GRANULARITY 8

#ifndef CYCLE_PER_US
#error Must define CYCLE_PER_US for the current machine in Makefile or elsewhere
#endif
#define CYCLE_PER_MS (CYCLE_PER_US * 1000L)
#define CYCLE_PER_S (CYCLE_PER_MS * 1000L)
#define FAIRLOCK_GRANULARITY (CYCLE_PER_MS * 2L)

#define readvol(lvalue) (*(volatile typeof(lvalue)*)(&lvalue))

#define spin_then_yield(limit, expr) while (1) { \
    int val, counter = 0;                        \
    while ((val = (expr)) && counter++ < limit); \
    if (!val) break; sched_yield(); }

static const int prio_to_weight[40] = {
 /* -20 */     88761,     71755,     56483,     46273,     36291,
 /* -15 */     29154,     23254,     18705,     14949,     11916,
 /* -10 */      9548,      7620,      6100,      4904,      3906,
 /*  -5 */      3121,      2501,      1991,      1586,      1277,
 /*   0 */      1024,       820,       655,       526,       423,
 /*   5 */       335,       272,       215,       172,       137,
 /*  10 */       110,        87,        70,        56,        45,
 /*  15 */        36,        29,        23,        18,        15,
};

