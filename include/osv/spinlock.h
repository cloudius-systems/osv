/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_SPINLOCK_H_
#define OSV_SPINLOCK_H_

#include <sys/cdefs.h>

__BEGIN_DECLS

// Spin lock. Use mutex instead, except where impossible:

typedef struct spinlock {
    bool _lock;
#ifdef __cplusplus
    // additional convenience methods for C++
    inline constexpr spinlock() : _lock(false) { }
    inline void lock();
    inline void unlock();
#endif
} spinlock_t;

static inline void spinlock_init(spinlock_t *sl)
{
    sl->_lock = false;
}
void spin_lock(spinlock_t *sl);
void spin_unlock(spinlock_t *sl);

__END_DECLS

#ifdef __cplusplus
void spinlock::lock()
{
    spin_lock(this);
}
void spinlock::unlock()
{
    spin_unlock(this);
}
#endif

#endif /* OSV_SPINLOCK_H_ */
