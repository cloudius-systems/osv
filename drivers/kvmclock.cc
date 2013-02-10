#include "clock.hh"
#include "msr.hh"
#include <osv/types.h>
#include "mmu.hh"
#include "string.h"

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
    virtual u64 time();
private:
    u64 wall_clock_boot();
    u64 system_time();
private:
    pvclock_wall_clock* _wall;
    pvclock_vcpu_time_info* _sys;  // FIXME: make percpu
};

kvmclock::kvmclock()
{
    _wall = new kvmclock::pvclock_wall_clock;
    _sys = new kvmclock::pvclock_vcpu_time_info;
    memset(_wall, 0, sizeof(_wall));
    memset(_sys, 0, sizeof(_sys));
    processor::wrmsr(msr::KVM_WALL_CLOCK_NEW, mmu::virt_to_phys(_wall));
    // FIXME: on each cpu
    processor::wrmsr(msr::KVM_SYSTEM_TIME_NEW, mmu::virt_to_phys(_sys) | 1);
}

u64 kvmclock::time()
{
    // FIXME: disable interrupts
    return wall_clock_boot() + system_time();
}

u64 kvmclock::wall_clock_boot()
{
    u32 v1, v2;
    u64 w;
    do {
        v1 = _wall->version;
        __sync_synchronize();
        w = u64(_wall->sec) * 1000000000 + _wall->nsec;
        __sync_synchronize();
        v2 = _wall->version;
    } while (v1 != v2);
    return w;
}

u64 kvmclock::system_time()
{
    u32 v1, v2;
    u64 time;
    do {
        v1 = _sys->version;
        __sync_synchronize();
        time = processor::rdtsc() - _sys->tsc_timestamp;
        if (_sys->tsc_shift >= 0) {
            time <<= _sys->tsc_shift;
        } else {
            time >>= -_sys->tsc_shift;
        }
        asm("mul %1; shrd $32, %%rdx, %0"
                : "+a"(time)
                : "rm"(u64(_sys->tsc_to_system_mul))
                : "rdx");
        time += _sys->system_time;
        __sync_synchronize();
        v2 = _sys->version;
    } while (v1 != v2);
    return time;
}

static __attribute__((constructor)) void setup_kvmclock()
{
    // FIXME: cpuid
    clock::register_clock(new kvmclock);
}
