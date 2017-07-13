/*
 * Copyright (C) 2017 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/prio.hh>
#include <osv/irqlock.hh>
#include <osv/mutex.h>
#include "clock.hh"
#include "rtc.hh"
#include <dev/hyperv/include/hyperv.h>

/**
 * This clock uses uses RTC to grab wall clock time and Partition Reference Counter MSR
 * (section 12.4 from https://github.com/Microsoft/Virtualization-Documentation/raw/master/tlfs/Hypervisor%20Top%20Level%20Functional%20Specification%20v5.0b.pdf)
 * as a TSC source.
 * TODO: The MSR is simulated so the call to read its value is quite expensive. Therefore
 * eventually we should implement more efficient clock using Hyper/V Reference TSC page
 * (see section 12.6 in the same document reference above).
 */
class hypervclock : public clock {
public:
    hypervclock();
    virtual s64 time() __attribute__((no_instrument_function));
    virtual s64 uptime() override __attribute__((no_instrument_function));
    virtual s64 boot_time() override __attribute__((no_instrument_function));
private:
    uint64_t _boot_wall;
    uint64_t _boot_count; // Single reference count represents 100 ns
};

hypervclock::hypervclock()
{
    auto r = new rtc();

    // In theory we should disable NMIs, but on virtual hardware, we can
    // relax that (This is specially true given our current NMI handler,
    // which will just halt us forever.
    irq_save_lock_type irq_lock;
    WITH_LOCK(irq_lock) {
        _boot_wall = r->wallclock_ns();
        _boot_count = hyperv_tc64_rdmsr();
    };
}

s64 hypervclock::time()
{
    return _boot_wall + (hyperv_tc64_rdmsr() - _boot_count) * 100;
}

s64 hypervclock::uptime()
{
    return (hyperv_tc64_rdmsr() - _boot_count) * 100;
}

s64 hypervclock::boot_time()
{
    return _boot_wall;
}

void __attribute__((constructor(init_prio::clock))) hyperv_init()
{
    if (processor::features().hyperv_clocksource) {
        clock::register_clock(new hypervclock);
    }
}
