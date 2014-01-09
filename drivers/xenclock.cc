/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "clock.hh"
#include "msr.hh"
#include <osv/types.h>
#include <osv/percpu.hh>
#include <osv/pvclock-abi.hh>
#include "mmu.hh"
#include "string.h"
#include "cpuid.hh"
#include "barrier.hh"
#include "xen.hh"
#include "debug.hh"
#include "prio.hh"
#include <preempt-lock.hh>

class xenclock : public clock {
public:
    xenclock();
    virtual s64 time() __attribute__((no_instrument_function));
    virtual s64 uptime() override __attribute__((no_instrument_function));
private:
    pvclock_wall_clock* _wall;
    static void setup_cpu();
    static bool _smp_init;
    static s64 _boot_systemtime;
    sched::cpu::notifier cpu_notifier;
    static u64 system_time();
};

bool xenclock::_smp_init = false;
s64 xenclock::_boot_systemtime = 0;

xenclock::xenclock()
    : cpu_notifier(&xenclock::setup_cpu)
{
    _wall = &xen::xen_shared_info.wc;
}

void xenclock::setup_cpu()
{
    _smp_init = true;
    _boot_systemtime = system_time();
}

s64 xenclock::time()
{
    int cpu = 0;
    pvclock_vcpu_time_info *sys;

    sched::preempt_disable();
    // For Xen, I am basically not sure if we can only compute the wall clock
    // once although it is very likely. I am leaving it like this until I can
    // go and make sure.
    auto r = pvclock::wall_clock_boot(_wall);
    if (_smp_init) {
        cpu = sched::cpu::current()->id;
    }
    sys = &xen::xen_shared_info.vcpu_info[cpu].time;
    r += pvclock::system_time(sys);
    sched::preempt_enable();
    return r;
}

u64 xenclock::system_time()
{
    WITH_LOCK(preempt_lock) {
        auto cpu = sched::cpu::current()->id;
        auto sys = &xen::xen_shared_info.vcpu_info[cpu].time;
        return pvclock::system_time(sys);
    }
}

s64 xenclock::uptime()
{
    if (_smp_init) {
        return system_time() - _boot_systemtime;
    } else {
        return 0;
    }
}

static __attribute__((constructor(init_prio::clock))) void setup_xenclock()
{
    // FIXME: find out if the HV supports positioning the vcpu structure
    // outside the shared structure, and keep going in that case.
    if (sched::cpus.size() > 32) {
        return;
    }

    if (processor::features().xen_clocksource) {
        clock::register_clock(new xenclock);
    }
}
