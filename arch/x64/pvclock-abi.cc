/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <osv/pvclock-abi.hh>
#include "processor.hh"
#include <osv/barrier.hh>

u64 pvclock::wall_clock_boot(pvclock_wall_clock *_wall)
{
    u32 v1, v2;
    u64 w;
    do {
        v1 = _wall->version;
        barrier();
        w = u64(_wall->sec) * 1000000000 + _wall->nsec;
        barrier();
        v2 = _wall->version;
    } while (v1 != v2);
    return w;
}

u64 pvclock::system_time(pvclock_vcpu_time_info *sys)
{
    u32 v1, v2;
    u64 time;
    u8 flags;
    do {
        v1 = sys->version;
        barrier();
        processor::lfence();
        time = sys->system_time +
               processor_to_nano(sys, processor::rdtsc() - sys->tsc_timestamp);
        flags = sys->flags;
        barrier();
        v2 = sys->version;
    } while ((v1 & 1) || v1 != v2);

    flags &= _valid_flags;

    if (flags & TSC_STABLE_BIT) {
        return time;
    }

    auto current_last = _last.load(std::memory_order_relaxed);
    do {
        if (time <= current_last) {
            return current_last;
        }
    } while (!_last.compare_exchange_weak(current_last, time, std::memory_order_relaxed));

    return time;
}
