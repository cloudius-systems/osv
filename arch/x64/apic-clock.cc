/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/clockevent.hh"
#include "drivers/clock.hh"
#include "exceptions.hh"
#include "apic.hh"

using namespace processor;

class apic_clock_events : public clock_event_driver {
public:
    explicit apic_clock_events();
    ~apic_clock_events();
    virtual void setup_on_cpu();
    virtual void set(std::chrono::nanoseconds nanos);
private:
    unsigned _vector;
};

apic_clock_events::apic_clock_events()
    : _vector(idt.register_handler([this] { _callback->fired(); }))
{
}

apic_clock_events::~apic_clock_events()
{
}

void apic_clock_events::setup_on_cpu()
{
    processor::apic->write(apicreg::TMDCR, 0xb); // divide by 1
    processor::apic->write(apicreg::TMICT, 0);
    processor::apic->write(apicreg::LVTT, _vector); // one-shot
}

void apic_clock_events::set(std::chrono::nanoseconds nanos)
{
    if (nanos.count() <= 0) {
        _callback->fired();
    } else {
        // FIXME: handle overflow
        apic->write(apicreg::TMICT, nanos.count());
    }
}

void __attribute__((constructor)) init_apic_clock()
{
    // FIXME: detect
    clock_event = new apic_clock_events;
}
