/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_DRIVERS_CLOCK_COMMON_HH_
#define OSV_DRIVERS_CLOCK_COMMON_HH_

#include <atomic>
#include <osv/sched.hh>
#include "clock.hh"

class pv_based_clock : public clock
{
public:
    pv_based_clock();
    virtual s64 time() __attribute__((no_instrument_function));
    virtual s64 uptime() override __attribute__((no_instrument_function));
    virtual s64 boot_time() override __attribute__((no_instrument_function));
private:
    std::atomic<bool> _smp_init;
    std::atomic<int> _boot_systemtime_init_counter;
    s64 _boot_systemtime;
    sched::cpu::notifier cpu_notifier;
    void setup_cpu();
protected:
    virtual u64 wall_clock_boot() = 0;
    virtual u64 system_time() = 0;
    virtual void init_on_cpu() {};
};

#endif
