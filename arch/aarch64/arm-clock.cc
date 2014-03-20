/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/clockevent.hh"
#include "drivers/clock.hh"
#include "arm-clock.hh"

using namespace processor;

class arm_clock_events : public clock_event_driver {
public:
    explicit arm_clock_events();
    ~arm_clock_events();
    virtual void setup_on_cpu();
    virtual void set(std::chrono::nanoseconds nanos);
private:
    unsigned _vector;
};


arm_clock_events::arm_clock_events()
{
}

arm_clock_events::~arm_clock_events()
{
}

void arm_clock_events::setup_on_cpu()
{
}

void arm_clock_events::set(std::chrono::nanoseconds nanos)
{
}

void __attribute__((constructor)) init_arm_clock()
{
    debug_early_entry("init_arm_clock()");
    clock_event = new arm_clock_events;
}
