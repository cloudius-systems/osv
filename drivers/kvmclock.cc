#include "clock.hh"
#include "msr.hh"
#include <osv/types.h>
#include "mmu.hh"
#include "string.h"
#include "cpuid.hh"
#include "barrier.hh"
#include <osv/percpu.hh>
#include <osv/pvclock-abi.hh>
#include "prio.hh"

class kvmclock : public clock {
public:
    kvmclock();
    virtual s64 time() __attribute__((no_instrument_function));
private:
    u64 wall_clock_boot();
    u64 system_time();
    static void setup_cpu();
private:
    static bool _smp_init;
    pvclock_wall_clock* _wall;
    u64  _wall_ns;
    static PERCPU(pvclock_vcpu_time_info, _sys);
    sched::cpu::notifier cpu_notifier;
};

bool kvmclock::_smp_init = false;
PERCPU(pvclock_vcpu_time_info, kvmclock::_sys);

kvmclock::kvmclock()
    : cpu_notifier(&kvmclock::setup_cpu)
{
    _wall = new pvclock_wall_clock;
    memset(_wall, 0, sizeof(*_wall));
    processor::wrmsr(msr::KVM_WALL_CLOCK_NEW, mmu::virt_to_phys(_wall));
    _wall_ns = wall_clock_boot();
}

void kvmclock::setup_cpu()
{
    memset(&*_sys, 0, sizeof(*_sys));
    processor::wrmsr(msr::KVM_SYSTEM_TIME_NEW, mmu::virt_to_phys(&*_sys) | 1);
    _smp_init = true;
}

s64 kvmclock::time()
{
    auto r = _wall_ns;
    // Due to problems in init order dependencies (the clock depends
    // on the scheduler, for percpu initialization, and vice-versa, for
    // idle thread initialization, don't loop up system time until at least
    // one cpu is initialized.
    if (_smp_init) {
        r += system_time();
    }
    return r;
}

u64 kvmclock::wall_clock_boot()
{
    return pvclock::wall_clock_boot(_wall);
}

u64 kvmclock::system_time()
{
    sched::preempt_disable();
    auto sys = &*_sys;  // avoid recaclulating address each access
    auto r = pvclock::system_time(sys);
    sched::preempt_enable();
    return r;
}

static __attribute__((constructor(CLOCK_INIT_PRIO))) void setup_kvmclock()
{
    // FIXME: old clocksource too?
    if (processor::features().kvm_clocksource2) {
        clock::register_clock(new kvmclock);
    }
}
