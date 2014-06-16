/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "clock-common.hh"

pv_based_clock::pv_based_clock()
    : _smp_init(false)
    , cpu_notifier([&] { setup_cpu(); })
{
}

void pv_based_clock::setup_cpu()
{
    init_on_cpu();

    std::call_once(_boot_systemtime_init_flag, [&] {
        _boot_systemtime = system_time();
        _smp_init.store(true, std::memory_order_release);
    });
}

s64 pv_based_clock::time()
{
    auto r = wall_clock_boot();
    // FIXME: during early boot, while _smp_init is still false, we don't
    // add system_time() so we return the host's boot time instead of the
    // current time. When _smp_init becomes true, the clock jumps forward
    // to the correct current time.
    // This happens due to problems in init order dependencies (the clock
    // depends on the scheduler, for percpu initialization, and vice-versa,
    // for idle thread initialization).
    if (_smp_init.load(std::memory_order_acquire)) {
        r += system_time();
    }
    return r;
}

s64 pv_based_clock::uptime()
{
    if (_smp_init.load(std::memory_order_acquire)) {
        return system_time() - _boot_systemtime;
    } else {
        return 0;
    }
}

s64 pv_based_clock::boot_time()
{
    // The following is time()-uptime():
    auto r = wall_clock_boot();
    if (_smp_init.load(std::memory_order_acquire)) {
        r += _boot_systemtime;
    }
    return r;
}
