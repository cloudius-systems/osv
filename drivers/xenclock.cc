/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "clock-common.hh"
#include "msr.hh"
#include <osv/types.h>
#include <osv/percpu.hh>
#include <osv/pvclock-abi.hh>
#include <osv/mmu.hh>
#include "string.h"
#include "cpuid.hh"
#include <osv/barrier.hh>
#define CONF_drivers_xen 1
#include <osv/xen.hh>
#include <osv/debug.hh>
#include <osv/prio.hh>
#include <osv/migration-lock.hh>

class xenclock : public pv_based_clock {
public:
    xenclock();
protected:
    virtual u64 wall_clock_boot();
    virtual u64 system_time();
    virtual u64 processor_to_nano(u64 ticks) override __attribute__((no_instrument_function));
private:
    pvclock_wall_clock* _wall;
    pvclock _pvclock;
};

xenclock::xenclock()
    : _pvclock(0)
{
    _wall = &xen::xen_shared_info.wc;
}

u64 xenclock::wall_clock_boot()
{
    return _pvclock.wall_clock_boot(_wall);
}

u64 xenclock::system_time()
{
    WITH_LOCK(migration_lock) {
        auto cpu = sched::cpu::current();
        auto cpu_id = cpu ? cpu->id : 0;
        auto sys = &xen::xen_shared_info.vcpu_info[cpu_id].time;
        return _pvclock.system_time(sys);
    }
}

u64 xenclock::processor_to_nano(u64 ticks)
{
    WITH_LOCK(migration_lock) {
        auto cpu = sched::cpu::current()->id;
        auto sys = &xen::xen_shared_info.vcpu_info[cpu].time;
        return pvclock::processor_to_nano(sys, ticks);
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
