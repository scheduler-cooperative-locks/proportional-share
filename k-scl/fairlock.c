#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/fairlock.h>
#include <asm/current.h>

/*
 * This value will change depending on the CPU-SPEED.
 * Please set the value accordingly.
 */
#define INACTIVE_THRESHOLD 2400000000 /* about 1 sec */

struct fairlock_waiter {
	unsigned long long banned_until;
	unsigned long long start_ticks;
	unsigned long long end_ticks;
	struct hlist_node hash;
	struct list_head list;
	pid_t pid;
};

inline struct fairlock_waiter *create_waiter(struct fairlock *lock)
{
	unsigned long long now;
	struct fairlock_waiter *waiter;
	pid_t pid;

	now = rdtsc();
	waiter = kmalloc(sizeof(struct fairlock_waiter), GFP_KERNEL);
	if (!waiter) {
		return waiter;
	}
	pid = get_current()->pid;
	waiter->pid = pid;
	waiter->banned_until = now;
	waiter->start_ticks = now;
	waiter->end_ticks = now;
	INIT_LIST_HEAD(&waiter->list);
	list_add_tail(&waiter->list, &lock->waiters);
	INIT_HLIST_NODE(&waiter->hash);
	hash_add(lock->waiters_lookup, &waiter->hash, pid);
	atomic_inc(&lock->num_threads);
	return waiter;
}

inline struct fairlock_waiter *retrieve_waiter(struct fairlock *lock)
{
	struct fairlock_waiter *waiter;
	pid_t pid = get_current()->pid;

	hash_for_each_possible(lock->waiters_lookup, waiter, hash, pid) {
		if (waiter->pid == pid)
			return waiter;
	}
	return NULL;
}

inline void fairlock_init(struct fairlock *lock)
{
	hash_init(lock->waiters_lookup);
	INIT_LIST_HEAD(&lock->waiters);
	lock->num_threads = (atomic_t) ATOMIC_INIT(0);
	lock->next_ticket = (atomic_t) ATOMIC_INIT(0);
	lock->now_serving = (atomic_t) ATOMIC_INIT(0);
}
EXPORT_SYMBOL(fairlock_init);

inline void fairlock_destroy(struct fairlock *lock)
{
	unsigned int end_ticket;

	end_ticket = atomic_fetch_inc(&lock->next_ticket);
	while (atomic_read(&lock->now_serving) != end_ticket);
}
EXPORT_SYMBOL(fairlock_destroy);

int fair_trylock(struct fairlock *lock)
{
	unsigned int my_ticket;
	unsigned int serving;
	struct fairlock_waiter *waiter;

	serving = atomic_read(&lock->now_serving);
	if (serving != atomic_cmpxchg(&lock->next_ticket, serving, serving + 1))
		return 0;
	my_ticket = serving;

	waiter = retrieve_waiter(lock);

	if (!waiter) {
		waiter = create_waiter(lock);
		if (!waiter) {
			return 0;
		}
	} else {
		if (waiter->end_ticks < waiter->banned_until && rdtsc() < waiter->banned_until) {
			atomic_inc(&lock->now_serving);
			return 0;
		}
		waiter->start_ticks = rdtsc();
	}
	lock->holder = waiter;
	return 1;
}
EXPORT_SYMBOL(fair_trylock);

void fair_lock(struct fairlock *lock)
{
	unsigned int my_ticket;
	struct fairlock_waiter *waiter;

	my_ticket = atomic_fetch_inc(&lock->next_ticket);
	while (atomic_read(&lock->now_serving) != my_ticket) {
		cond_resched();
	}

	waiter = retrieve_waiter(lock);

	if (!waiter) {
		waiter = create_waiter(lock);
		if (!waiter) {
			panic("Unable to allocate memory for fairlock waiter\n");
		}
		lock->holder = waiter;
	} else {
		if (waiter->end_ticks < waiter->banned_until && rdtsc() < waiter->banned_until) {
			atomic_inc(&lock->now_serving);
			do {
				cond_resched();
			} while (rdtsc() < waiter->banned_until);
			my_ticket = atomic_fetch_inc(&lock->next_ticket);
			while (atomic_read(&lock->now_serving) != my_ticket);
		}
		waiter->start_ticks = rdtsc();
		lock->holder = waiter;
	}
}
EXPORT_SYMBOL(fair_lock);

void fair_unlock(struct fairlock *lock)
{
	struct fairlock_waiter *waiter, *prev_waiter, *tmp;
	unsigned int num_threads;
	unsigned long long cs_length;
	unsigned long long now;

	waiter = lock->holder;
	now = rdtsc();
	waiter->end_ticks = now;
	num_threads = atomic_read(&lock->num_threads);
	if (num_threads > 1) {
		cs_length = now - waiter->start_ticks;
		waiter->banned_until += cs_length * num_threads;

		list_for_each_entry_safe_reverse(prev_waiter, tmp, &waiter->list, list) {
			if (&prev_waiter->list == &lock->waiters)
				continue;
			if (prev_waiter->end_ticks < now - INACTIVE_THRESHOLD) {
				list_del(&prev_waiter->list);
				hash_del(&prev_waiter->hash);
				kfree(prev_waiter);
				atomic_dec(&lock->num_threads);
			}
		}
	} else {
		waiter->banned_until = now;
	}
	atomic_inc(&lock->now_serving);
}
EXPORT_SYMBOL(fair_unlock);

