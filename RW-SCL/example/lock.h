#ifndef __LOCK_H__
#define __LOCK_H__

#ifdef PTHREAD_RW
#include <pthread.h>
typedef pthread_rwlock_t lock_t;
#define lock_init(plock, attr) pthread_rwlock_init(plock, attr)
#define lock_writer_lock(plock) pthread_rwlock_wrlock(plock)
#define lock_reader_lock(plock) pthread_rwlock_rdlock(plock)
#define lock_writer_unlock(plock) pthread_rwlock_unlock(plock)
#define lock_reader_unlock(plock) pthread_rwlock_unlock(plock)
#define lock_destroy(plock) pthread_rwlock_destroy(plock)

#elif RWLOCK_SCL
#include "rwlock.h"
typedef rwlock_t lock_t;
#define lock_init(plock) rwlock_init(plock)
#define lock_writer_lock(plock) rwlock_writer_lock(plock)
#define lock_reader_lock(plock) rwlock_reader_lock(plock)
#define lock_writer_unlock(plock) rwlock_writer_unlock(plock)
#define lock_reader_unlock(plock) rwlock_reader_unlock(plock)
#define lock_destroy(plock) rwlock_destroy(plock)

#elif RWLOCK_SCL_ORIG
#include "orig_rwlock.h"
typedef rwlock_t lock_t;
#define lock_init(plock) rwlock_init(plock)
#define lock_writer_lock(plock) rwlock_writer_lock(plock)
#define lock_reader_lock(plock) rwlock_reader_lock(plock)
#define lock_writer_unlock(plock) rwlock_writer_unlock(plock)
#define lock_reader_unlock(plock) rwlock_reader_unlock(plock)
#define lock_destroy(plock) rwlock_destroy(plock)

#endif

#endif // __LOCK_H__

