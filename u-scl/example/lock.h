#ifndef __LOCK_H__
#define __LOCK_H__

#ifdef MUTEX
#include <pthread.h>
typedef pthread_mutex_t lock_t;
#define lock_init(plock) pthread_mutex_init(plock, NULL)
#define lock_acquire(plock) pthread_mutex_lock(plock)
#define lock_release(plock) pthread_mutex_unlock(plock)

#elif SPIN
#include <pthread.h>
typedef pthread_spinlock_t lock_t;
#define lock_init(plock) pthread_spin_init(plock, PTHREAD_PROCESS_PRIVATE)
#define lock_acquire(plock) pthread_spin_lock(plock)
#define lock_release(plock) pthread_spin_unlock(plock)

#elif FAIRLOCK
#include "fairlock.h"
typedef fairlock_t lock_t;
#define lock_init(plock) fairlock_init(plock)
#define lock_acquire(plock) fairlock_acquire(plock)
#define lock_release(plock) fairlock_release(plock)

#endif

#endif // __LOCK_H__

