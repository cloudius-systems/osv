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

//Please note this spinlock disables/enables premption
//unlike the np_spinlock below
typedef struct spinlock {
    bool _lock;
#ifdef __cplusplus
    // additional convenience methods for C++
    inline constexpr spinlock() : _lock(false) {}
    inline bool trylock();
    inline void lock();
    inline void unlock();
#endif
} spinlock_t;

static inline void spinlock_init(spinlock_t *sl)
{
    sl->_lock = false;
}
void spin_lock(spinlock_t *sl);
bool spin_trylock(spinlock_t *sl);
void spin_unlock(spinlock_t *sl);

//Spinlock intended to be used when interrupts are disabled
//typically in the early device drivers initialization phase or
//in the interrupt handlers. Another use case is when we do not
//want to disable preemption.
//It differs from regular spinlock above, in that it does
//not disable preemption on entry and does not enable it on exit.
typedef struct np_spinlock {
    bool _lock;
#ifdef __cplusplus
    // additional convenience methods for C++
    inline constexpr np_spinlock() : _lock(false) {}
    inline bool trylock();
    inline void lock();
    inline void unlock();
#endif
} np_spinlock_t;

static inline void np_spinlock_init(np_spinlock_t *sl)
{
    sl->_lock = false;
}
void np_spin_lock(np_spinlock_t *sl);
bool np_spin_trylock(np_spinlock_t *sl);
void np_spin_unlock(np_spinlock_t *sl);

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

void np_spinlock::lock()
{
    np_spin_lock(this);
}
void np_spinlock::unlock()
{
    np_spin_unlock(this);
}
#endif

#endif /* OSV_SPINLOCK_H_ */
