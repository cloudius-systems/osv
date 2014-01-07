/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/spinlock.h>
#include <sched.hh>

void spin_lock(spinlock_t *sl)
{
    sched::preempt_disable();
    while (__sync_lock_test_and_set(&sl->_lock, 1))
        ;
}

void spin_unlock(spinlock_t *sl)
{
    __sync_lock_release(&sl->_lock, 0);
    sched::preempt_enable();
}
