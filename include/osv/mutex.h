/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MUTEX_H_
#define MUTEX_H_

#include <sys/cdefs.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>

// To use the spin-lock-based mutex instead of lockfree::mutex everywhere,
// change #define LOCKFREE_MUTEX here to #undef.
#define LOCKFREE_MUTEX

#define LOCKFREE_MUTEX_ALIGN void*
#define LOCKFREE_MUTEX_SIZE 40
#ifdef __cplusplus
/** C++ **/
#include <lockfree/mutex.hh>
typedef lockfree::mutex mutex;
typedef lockfree::mutex mutex_t;
static_assert(sizeof(mutex) == LOCKFREE_MUTEX_SIZE,
        "LOCKFREE_MUTEX_SIZE should match lockfree::mutex");
static_assert(alignof(mutex) == alignof(LOCKFREE_MUTEX_ALIGN),
        "LOCKFREE_MUTEX_ALIGN should match alignment of lockfree::mutex");
static inline void mutex_lock(mutex_t* m)
{
    m->lock();
}
static inline bool mutex_trylock(mutex_t* m)
{
    return m->try_lock();
}
static inline void mutex_unlock(mutex_t* m)
{
    m->unlock();
}
static inline bool mutex_owned(mutex_t* m)
{
    return m->owned();
}
#else
/** C **/
typedef struct mutex {
    union {
        char forsize[LOCKFREE_MUTEX_SIZE];
        LOCKFREE_MUTEX_ALIGN foralignment;
    };
} mutex_t;
void lockfree_mutex_lock(void *m);
void lockfree_mutex_unlock(void *m);
bool lockfree_mutex_try_lock(void *m);
bool lockfree_mutex_owned(void *m);
static inline void mutex_lock(mutex_t *m) { lockfree_mutex_lock(m); }
static inline void mutex_unlock(mutex_t *m) { lockfree_mutex_unlock(m); }
static inline bool mutex_trylock(mutex_t *m) { return lockfree_mutex_try_lock(m); }
static inline bool mutex_owned(mutex_t *m) { return lockfree_mutex_owned(m); }
#endif
/** both C and C++ code currently use these, though they should be C-only  **/
static inline void mutex_init(mutex_t* m) { memset(m, 0, sizeof(mutex_t)); }
static inline void mutex_destroy(mutex_t* m) { }
#define MUTEX_INITIALIZER   {}

#ifdef __cplusplus
#include <mutex>
#include <cstdlib>

template <class lock_type>
struct lock_guard_for_with_lock : std::lock_guard<lock_type> {
    lock_guard_for_with_lock(lock_type& lock) : std::lock_guard<lock_type>(lock) {}
    operator bool() const { return false; }
};

#define WITH_LOCK(_wl_lock) \
    if (lock_guard_for_with_lock<decltype(_wl_lock)> _wl_lock_guard{_wl_lock}) \
        std::abort(); \
    else /* locked statement comes here */


// like std::lock_guard<>, but drops the lock temporarily
// instead of acquiring it
template <class lock_type>
struct lock_guard_for_drop_lock {
    lock_guard_for_drop_lock(lock_type& lock) : l(lock) { l.unlock(); }
    ~lock_guard_for_drop_lock() { l.lock(); }
    operator bool() const { return false; }
    lock_type& l;
};

// Used for temporarily dropping a lock
// Note: doesn't deal well with recursive locks
#define DROP_LOCK(_dl_lock) \
    if (lock_guard_for_drop_lock<decltype(_dl_lock)> _dl_lock_guard{_dl_lock}) \
        std::abort(); \
    else /* unlocked statement comes here */

#endif

#endif /* MUTEX_H_ */
