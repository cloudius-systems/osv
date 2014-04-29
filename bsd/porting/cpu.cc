/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include <bsd/porting/netport.h>

extern "C" int get_cpuid(void)
{
    return sched::cpu::current()->id;
}

/*
 * Return contents of in-cpu fast counter as a sort of "bogo-time"
 * for random-harvesting purposes.
 */
extern "C" uint64_t get_cyclecount(void)
{
    return get_ticks();
}
