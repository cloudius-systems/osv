#include "clock.hh"
#include "msr.hh"
#include <osv/types.h>
#include "mmu.hh"
#include "string.h"
#include "cpuid.hh"
#include "barrier.hh"
#include <osv/percpu.hh>

class kvmclock : public clock {
private:
    struct pvclock_wall_clock {
            u32   version;
            u32   sec;
            u32   nsec;
    } __attribute__((__packed__));
    struct pvclock_vcpu_time_info {
             u32   version;
             u32   pad0;
             u64   tsc_timestamp;
             u64   system_time;
             u32   tsc_to_system_mul;
             s8    tsc_shift;
             u8    flags;
             u8    pad[2];
     } __attribute__((__packed__)); /* 32 bytes */
public:
    kvmclock();
    virtual u64 time() __attribute__((no_instrument_function));
private:
    u64 wall_clock_boot();
    u64 system_time();
    static void setup_cpu();
private:
    static bool _smp_init;
    pvclock_wall_clock* _wall;
    static PERCPU(pvclock_vcpu_time_info, _sys);
    sched::cpu::notifier cpu_notifier;
};

bool kvmclock::_smp_init = false;
PERCPU(kvmclock::pvclock_vcpu_time_info, kvmclock::_sys);

kvmclock::kvmclock()
    : cpu_notifier(&kvmclock::setup_cpu)
{
    _wall = new kvmclock::pvclock_wall_clock;
    memset(_wall, 0, sizeof(*_wall));
    processor::wrmsr(msr::KVM_WALL_CLOCK_NEW, mmu::virt_to_phys(_wall));
}

void kvmclock::setup_cpu()
{
    memset(&*_sys, 0, sizeof(*_sys));
    processor::wrmsr(msr::KVM_SYSTEM_TIME_NEW, mmu::virt_to_phys(&*_sys) | 1);
    _smp_init = true;
}

u64 kvmclock::time()
{
    sched::preempt_disable();
    auto r = wall_clock_boot();
    // Due to problems in init order dependencies (the clock depends
    // on the scheduler, for percpu initialization, and vice-versa, for
    // idle thread initialization, don't loop up system time until at least
    // one cpu is initialized.
    if (_smp_init) {
        r += system_time();
    }
    sched::preempt_enable();
    return r;
}

u64 kvmclock::wall_clock_boot()
{
    u32 v1, v2;
    u64 w;
    do {
        v1 = _wall->version;
        barrier();
        w = u64(_wall->sec) * 1000000000 + _wall->nsec;
        barrier();
        v2 = _wall->version;
    } while (v1 != v2);
    return w;
}

u64 kvmclock::system_time()
{
    u32 v1, v2;
    u64 time;
    auto sys = &*_sys;  // avoid recaclulating address each access
    do {
        v1 = sys->version;
        barrier();
        time = processor::rdtsc() - sys->tsc_timestamp;
        if (sys->tsc_shift >= 0) {
            time <<= sys->tsc_shift;
        } else {
            time >>= -sys->tsc_shift;
        }
        asm("mul %1; shrd $32, %%rdx, %0"
                : "+a"(time)
                : "rm"(u64(sys->tsc_to_system_mul))
                : "rdx");
        time += sys->system_time;
        barrier();
        v2 = sys->version;
    } while (v1 != v2);
    return time;
}

static __attribute__((constructor)) void setup_kvmclock()
{
    // FIXME: old clocksource too?
    if (processor::features().kvm_clocksource2) {
        clock::register_clock(new kvmclock);
    }
}
