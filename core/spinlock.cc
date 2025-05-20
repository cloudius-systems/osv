/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/spinlock.h>
#include <osv/sched.hh>

void spin_lock(spinlock_t *sl)
{
    sched::preempt_disable();
    while (__sync_lock_test_and_set(&sl->_lock, 1)) {
        while (sl->_lock) {
#ifdef __x86_64__
            __asm __volatile("pause");
#endif
#ifdef __aarch64__
            __asm __volatile("isb sy");
#endif
        }
    }
}

bool spin_trylock(spinlock_t *sl)
{
    sched::preempt_disable();
    if (__sync_lock_test_and_set(&sl->_lock, 1)) {
        sched::preempt_enable();
        return false;
    }
    return true;
}

void spin_unlock(spinlock_t *sl)
{
    __sync_lock_release(&sl->_lock, 0);
    sched::preempt_enable();
}

void np_spin_lock(np_spinlock_t *sl)
{
    while (__sync_lock_test_and_set(&sl->_lock, 1)) {
        while (sl->_lock) {
#ifdef __x86_64__
            __asm __volatile("pause");
#endif
#ifdef __aarch64__
            __asm __volatile("isb sy");
#endif
        }
    }
}

bool np_spin_trylock(np_spinlock_t *sl)
{
    if (__sync_lock_test_and_set(&sl->_lock, 1)) {
        return false;
    }
    return true;
}

void np_spin_unlock(np_spinlock_t *sl)
{
    __sync_lock_release(&sl->_lock, 0);
}
