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

/**
 * Convenient literals for specifying std::chrono::duration's.
 *
 * C++14 will support std::chrono::duration literals, used as in the following
 * example:
 * @code
 *     using namespace std::literals::chrono_literals;
 *     auto d = 100ns;
 * @endcode
 * Because these are not yet available to us, we implement a similar feature
 * but using different names:
 * @code
 *     using namespace osv::clock::literals;
 *     auto d = 100_ns;
 * @endcode
 */
namespace literals {
/** Nanoseconds */
constexpr std::chrono::nanoseconds operator "" _ns(unsigned long long c) {
    return std::chrono::nanoseconds(c);
}
/** Microseconds */
constexpr std::chrono::microseconds operator "" _us(unsigned long long c) {
    return std::chrono::microseconds(c);
}
/** Milliseconds */
constexpr std::chrono::milliseconds operator "" _ms(unsigned long long c) {
    return std::chrono::milliseconds(c);
}
/** Seconds */
constexpr std::chrono::seconds operator "" _s(unsigned long long c) {
    return std::chrono::seconds(c);
}
}

}
}

#endif /* OSV_CLOCK_HH_ */
