/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include <osv/export.h>
#include <bsd/porting/netport.h>
#include <machine/cpu.h>
#include "processor.hh"

// ZFS (both the in-tree compat layer and the OpenZFS module) sizes its per-CPU
// arrays as MAXCPU and indexes them by the current CPU id, so MAXCPU must track
// the scheduler's max_cpus. This TU includes both headers, so assert it here.
static_assert(MAXCPU == sched::max_cpus,
              "MAXCPU (bsd/porting/netport.h) must equal sched::max_cpus");

extern "C" OSV_LIBSOLARIS_API int get_cpuid(void)
{
    return sched::cpu::current()->id;
}

extern "C" OSV_LIBSOLARIS_API unsigned int sched_current_cpu(void)
{
    return (unsigned int)sched::cpu::current()->id;
}

uint64_t get_cyclecount(void)
{
    return processor::ticks();
}
