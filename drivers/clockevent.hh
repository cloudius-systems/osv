/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CLOCKEVENT_HH_
#define CLOCKEVENT_HH_

#include <osv/types.h>
#include <chrono>
#include <drivers/clock.hh>

class clock_event_callback {
public:
    virtual ~clock_event_callback();
    // note: must always be called on the same cpu that the timer was set on
    virtual void fired() = 0;
};

class clock_event_driver {
public:
    virtual ~clock_event_driver();
    virtual void setup_on_cpu() = 0;
    // set() is cpu-local: each processor has its own timer
    virtual void set(std::chrono::nanoseconds time) = 0;
    inline void set(s64 time) {
        s64 now = clock::get()->time();
        set(std::chrono::nanoseconds(time - now));
    }
    void set_callback(clock_event_callback* callback);
    clock_event_callback* callback() const;
protected:
    clock_event_callback* _callback;

};
extern clock_event_driver* clock_event;


#endif /* CLOCKEVENT_HH_ */
