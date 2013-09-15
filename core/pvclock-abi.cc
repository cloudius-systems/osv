/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <osv/pvclock-abi.hh>
#include "processor.hh"
#include "barrier.hh"

namespace pvclock {

u64 wall_clock_boot(pvclock_wall_clock *_wall)
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

u64 system_time(pvclock_vcpu_time_info *sys)
{
    u32 v1, v2;
    u64 time;
    do {
        v1 = sys->version;
        barrier();
        time = processor::rdtsc() - sys->tsc_timestamp;
        if (sys->tsc_shift >= 0) {
            time <<= sys->tsc_shift;
        } else {
            time >>= -sys->tsc_shift;
        }
        asm("mul %1; shrd $32, %%rdx, %0"
                : "+a"(time)
                : "rm"(u64(sys->tsc_to_system_mul))
                : "rdx");
        time += sys->system_time;
        barrier();
        v2 = sys->version;
    } while (v1 != v2);
    return time;
}
};
