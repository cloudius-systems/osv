/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/clockevent.hh"
#include "drivers/clock.hh"
#include "arm-clock.hh"
#include "exceptions.hh"

#include <osv/debug.hh>
#include <osv/prio.hh>

using namespace processor;

#define NANO_PER_SEC 1000000000
#define MHZ 1000000

class arm_clock : public clock {
public:
    arm_clock();
    /* Get the current value of the nanoseconds since boot */
    virtual s64 uptime();
    /* Get the nanoseconds since the epoc */
    virtual s64 time();
    /* Return current estimate of wall-clock time at OSV's boot. */
    virtual s64 boot_time();
protected:
    u32 freq_hz;  /* frequency in Hz (updates per second) */

    friend class arm_clock_events;
};

arm_clock::arm_clock() {
    asm volatile ("mrs %0, cntfrq_el0; isb; " : "=r"(freq_hz) :: "memory");
    /* spec documents a typical range of 1-50 MHZ, but foundation model
       seems to run already at 100 MHZ, so allow max 500 MHZ */
    if (freq_hz < 1 * MHZ || freq_hz > 500 * MHZ) {
        debug_early_u64("arm_clock(): read invalid frequency ", freq_hz);
        abort();
    }
    debug_early_u64("arm_clock(): frequency read as ", freq_hz);
}

static __attribute__((constructor(init_prio::clock))) void setup_arm_clock()
{
    debug_early_entry("setup_arm_clock()");
    clock::register_clock(new arm_clock);
}

s64 arm_clock::uptime()
{
    u64 cntvct;
    asm volatile ("isb; mrs %0, cntvct_el0; isb; " : "=r"(cntvct) :: "memory");

    cntvct = ((__uint128_t)cntvct * NANO_PER_SEC) / this->freq_hz;
    return cntvct;
}

s64 arm_clock::time()
{
    return uptime();
}

s64 arm_clock::boot_time()
{
    return uptime();
}

class arm_clock_events : public clock_event_driver {
public:
    arm_clock_events();
    ~arm_clock_events();
    virtual void setup_on_cpu();
    virtual void set(std::chrono::nanoseconds nanos);

    unsigned int read_ctl();
    void write_ctl(unsigned int cntv_ctl);
    unsigned int read_tval();
    void write_tval(unsigned int cntv_tval);

    static void irq_handler(struct interrupt_desc *);

    unsigned int irqid; /* global InterruptID */
};

arm_clock_events::arm_clock_events()
{
    /* XXX hardcoded, and also note that on the hardware it's
       level-sensitive, but kvm/qemu make it edge for the guest */
    this->irqid = 27;
    idt.register_handler(this->irqid, &arm_clock_events::irq_handler,
                         gic::irq_type::IRQ_TYPE_EDGE);
}

void arm_clock_events::irq_handler(struct interrupt_desc *desc)
{
    clock_event->callback()->fired();
}

arm_clock_events::~arm_clock_events()
{
}

void arm_clock_events::setup_on_cpu()
{
}

unsigned int arm_clock_events::read_ctl()
{
    unsigned int cntv_ctl;
    asm volatile ("isb; mrs %0, cntv_ctl_el0; isb;" : "=r"(cntv_ctl)
                  :: "memory");
    return cntv_ctl;
}

void arm_clock_events::write_ctl(unsigned int cntv_ctl)
{
    asm volatile ("isb; msr cntv_ctl_el0, %0; isb;" :: "r"(cntv_ctl)
                  : "memory");
}

unsigned int arm_clock_events::read_tval()
{
    unsigned int cntv_tval;
    asm volatile ("isb; mrs %0, cntv_tval_el0; isb;" : "=r"(cntv_tval)
                  :: "memory");
    return cntv_tval;
}

void arm_clock_events::write_tval(unsigned int cntv_tval)
{
    asm volatile ("isb; msr cntv_tval_el0, %0; isb;" :: "r"(cntv_tval)
                  : "memory");
}

void arm_clock_events::set(std::chrono::nanoseconds nanos)
{
    if (nanos.count() <= 0) {
        _callback->fired();
    } else {
        u64 tval = nanos.count();
        class arm_clock *c = static_cast<arm_clock *>(clock::get());
        tval = ((__uint128_t)tval * c->freq_hz) / NANO_PER_SEC;

        u32 ctl = this->read_ctl();
        ctl |= 1;  /* set enable */
        ctl &= ~2; /* unmask timer interrupt */
        this->write_tval(tval);
        this->write_ctl(ctl);
    }
}

void __attribute__((constructor)) setup_arm_clock_events()
{
    arm_clock_events *timer;
    debug_early_entry("setup_arm_clock_events()");
    clock_event = timer = new arm_clock_events;

    u32 ctl = timer->read_ctl();
    ctl &= ~0x7;
    timer->write_ctl(ctl);
}
