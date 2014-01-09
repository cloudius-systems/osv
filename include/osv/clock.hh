/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_CLOCK_HH_
#define OSV_CLOCK_HH_

#include <drivers/clock.hh>
#include <chrono>

namespace osv {
/**
 * OSv Clock namespace
 */
namespace clock {

/**
 * Nanosecond-resolution monotonic uptime clock.
 *
 * The monotonic clock can only go forward, and measures, in nanoseconds,
 * the time that has passed since OSv's boot.
 *
 * This class is very similar to std::chrono::steady_clock, except the
 * latter is actually implemented on top of this one with clock_gettime()
 * in the middle, so this class is faster.
 */
class uptime {
public:
    typedef std::chrono::nanoseconds duration;
    typedef std::chrono::time_point<osv::clock::uptime> time_point;
    /**
     * Get the current value of the nanosecond-resolution monotonic clock.
     *
     * This is done using the most efficient clock available from the host.
     * It can be a very efficient paravirtual clock (kvmclock or xenclock),
     * or at last resort, the hpet clock (emulated by the host).
     */
    static time_point now() {
        return time_point(duration(::clock::get()->uptime()));
    }
};
}
}


#endif /* OSV_CLOCK_HH_ */
