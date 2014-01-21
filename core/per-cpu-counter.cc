/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/per-cpu-counter.hh>
#include <osv/mutex.h>
#include <osv/debug.hh>

void per_cpu_counter::increment()
{
    sched::preempt_disable();
    ++*_counter;
    sched::preempt_enable();
}

ulong per_cpu_counter::read()
{
    ulong sum = 0;
    for (auto cpu : sched::cpus) {
        sum += *_counter.for_cpu(cpu);
    }
    return sum;
}
