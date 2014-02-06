/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_PVCLOCK_ABI_H_
#define _OSV_PVCLOCK_ABI_H_
#include <osv/types.h>

struct pvclock_wall_clock {
        u32   version;
        u32   sec;
        u32   nsec;
} __attribute__((__packed__));

struct pvclock_vcpu_time_info {
         u32   version;
         u32   pad0;
         u64   tsc_timestamp;
         u64   system_time;
         u32   tsc_to_system_mul;
         s8    tsc_shift;
         u8    flags;
         u8    pad[2];
} __attribute__((__packed__)); /* 32 bytes */

namespace pvclock {

inline u64 processor_to_nano(pvclock_vcpu_time_info *sys, u64 time)
{
    if (sys->tsc_shift >= 0) {
        time <<= sys->tsc_shift;
    } else {
        time >>= -sys->tsc_shift;
    }
    asm("mul %1; shrd $32, %%rdx, %0"
            : "+a"(time)
            : "rm"(u64(sys->tsc_to_system_mul))
            : "rdx");
    return time;
}

u64 wall_clock_boot(pvclock_wall_clock *_wall);
u64 system_time(pvclock_vcpu_time_info *sys);
};
#endif
