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

class xenclock : public clock {
public:
    xenclock();
    virtual u64 time() __attribute__((no_instrument_function));
private:
    pvclock_wall_clock* _wall;
    static void setup_cpu();
    static bool _smp_init;
    sched::cpu::notifier cpu_notifier;
};

bool xenclock::_smp_init = false;

xenclock::xenclock()
    : cpu_notifier(&xenclock::setup_cpu)
{
    _wall = &xen::xen_shared_info.wc;
}

void xenclock::setup_cpu()
{
    _smp_init = true;
}

u64 xenclock::time()
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

static __attribute__((constructor)) void setup_xenclock()
{
    if (processor::features().xen_clocksource) {
        clock::register_clock(new xenclock);
    }
}
