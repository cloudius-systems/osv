/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "clock-common.hh"
#include "msr.hh"
#include <osv/types.h>
#include <osv/mmu.hh>
#include "string.h"
#include "cpuid.hh"
#include <osv/barrier.hh>
#include <osv/percpu.hh>
#include <osv/pvclock-abi.hh>
#include <osv/prio.hh>
#include <osv/migration-lock.hh>
#include <mutex>
#include <atomic>

class kvmclock : public pv_based_clock {
public:
    kvmclock();
    virtual u64 processor_to_nano(u64 ticks) override __attribute__((no_instrument_function));
    static bool probe();
protected:
    virtual u64 wall_clock_boot();
    virtual u64 system_time();
    virtual void init_on_cpu();
private:
    static bool _new_kvmclock_msrs;
    pvclock_wall_clock* _wall;
    static percpu<pvclock_vcpu_time_info> _sys;
    pvclock _pvclock;
};

bool kvmclock::_new_kvmclock_msrs = true;
PERCPU(pvclock_vcpu_time_info, kvmclock::_sys);

static u8 get_pvclock_flags()
{
    u8 flags = 0;
    if (processor::features().kvm_clocksource_stable) {
        flags |= pvclock::TSC_STABLE_BIT;
    }
    return flags;
}

kvmclock::kvmclock()
    : _pvclock(get_pvclock_flags())
{
    auto wall_time_msr = (_new_kvmclock_msrs) ?
                         msr::KVM_WALL_CLOCK_NEW : msr::KVM_WALL_CLOCK;
    _wall = new pvclock_wall_clock;
    memset(_wall, 0, sizeof(*_wall));
    processor::wrmsr(wall_time_msr, mmu::virt_to_phys(_wall));
}

void kvmclock::init_on_cpu()
{
    auto system_time_msr = (_new_kvmclock_msrs) ?
                           msr::KVM_SYSTEM_TIME_NEW : msr::KVM_SYSTEM_TIME;
    memset(&*_sys, 0, sizeof(*_sys));
    processor::wrmsr(system_time_msr, mmu::virt_to_phys(&*_sys) | 1);
}

bool kvmclock::probe()
{
    if (processor::features().kvm_clocksource2) {
        return true;
    }
    if (processor::features().kvm_clocksource) {
        _new_kvmclock_msrs = false;
        return true;
    }
    return false;
}

u64 kvmclock::wall_clock_boot()
{
    return _pvclock.wall_clock_boot(_wall);
}

u64 kvmclock::system_time()
{
    WITH_LOCK(migration_lock) {
        auto sys = &*_sys;  // avoid recalculating address each access
        return _pvclock.system_time(sys);
    }
}

u64 kvmclock::processor_to_nano(u64 ticks)
{
    return pvclock::processor_to_nano(&*_sys, ticks);
}

static __attribute__((constructor(init_prio::clock))) void setup_kvmclock()
{
    if (kvmclock::probe()) {
        clock::register_clock(new kvmclock);
    }
}
