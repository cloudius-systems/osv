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

extern "C" OSV_LIBSOLARIS_API int get_cpuid(void)
{
    return sched::cpu::current()->id;
}

uint64_t get_cyclecount(void)
{
    return processor::ticks();
}
