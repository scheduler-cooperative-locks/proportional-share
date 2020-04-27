
#ifndef __LINUX_FAIRLOCK_H
#define __LINUX_FAIRLOCK_H

#include <linux/atomic.h>
#include <linux/hashtable.h>

struct fairlock_waiter;

struct fairlock {
	DECLARE_HASHTABLE(waiters_lookup, 8);
	struct list_head waiters;
	atomic_t num_threads;
	atomic_t next_ticket;
	atomic_t now_serving;
	struct fairlock_waiter *holder;
};

extern void fairlock_init(struct fairlock *lock);
extern void fairlock_destroy(struct fairlock *lock);
extern int fair_trylock(struct fairlock *lock);
extern void fair_lock(struct fairlock *lock);
extern void fair_unlock(struct fairlock *lock);

#endif /* __LINUX_FAIRLOCK_H */

