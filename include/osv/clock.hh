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
#include <sys/time.h>

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
 * Nanosecond-resolution wall clock.
 *
 * The wall clock tells what humans think of "the current time" (Posix calls
 * it the "realtime clock"), and is returned as the number of nanoseconds
 * since the Unix epoch (midnight UTC, Jan 1st, 1970).
 *
 * This clock may be influenced by modifications to the host time (either
 * NTP or manual changes to the host's clock), and is not necessarily
 * monotonic.
 *
 * This class is very similar to std::chrono::system_clock, except the
 * latter is actually implemented on top of this one with clock_gettime()
 * in the middle, so this class is faster.
 */
class wall {
public:
    typedef std::chrono::nanoseconds duration;
    typedef std::chrono::time_point<osv::clock::wall> time_point;
    /**
     * Get the current value of the nanosecond-resolution wall clock.
     *
     * This is done using the most efficient clock available from the host.
     * It can be a very efficient paravirtual clock (kvmclock or xenclock),
     * or at last resort, the hpet clock (emulated by the host).
     */
    static time_point now() {
        return time_point(duration(::clock::get()->time()));
    }
    /*
     * Return current estimate of wall-clock time at OSV's boot.
     *
     * This is defined as the current difference between uptime::now()
     * and wall::now(), except that it is calculated faster, and
     * instantaneously (without taking the two clocks at slightly different
     * times).
     *
     * Note that boot_time() is not necessarily constant. Rather, if we (or
     * more likely, the host) adjust the wall-clock time, boot_time() will be
     * adjusted so that always boot_time() + uptime::now() = wall::now().
     */
    static time_point boot_time() {
        return time_point(duration(::clock::get()->boot_time()));
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

// Strangely, C++11 does not provide std::abs on std::chrono::duration.
// But it's easy to fix that lack:
namespace std {
template<typename Rep, typename Period>
std::chrono::duration<Rep,Period> abs(std::chrono::duration<Rep, Period> d)
{
    return std::chrono::duration<Rep,Period>(std::abs(d.count()));
}
}

// Durations cannot be compared with unit-less integers - one cannot just
// check d < 7, and need to do something like d < 7_ms. Unfortunately, this
// also applies to zero: d < 0 won't work, and users need to add a unit to
// the zero. Of course, any unit would work: d < 0_ms, or d < 0_ns, or
// any other time unit, will be equivalent.
// The following trick will allow comparing a duration to 0 without needing to
// add a unit, while still leaving comparison with a non-zero integer as an
// error. This trick works by allowing to compare a duration with nullptr_t,
// and noting that the compiler allows implicit conversion of the constant 0,
// but not other integer constants, to nullptr_t.

template<typename Rep, typename Period>
inline bool operator<=(std::chrono::duration<Rep, Period> d, std::nullptr_t)
{
    return d.count() <= 0;
}

template<typename Rep, typename Period>
inline bool operator<(std::chrono::duration<Rep, Period> d, std::nullptr_t)
{
    return d.count() < 0;
}

template<typename Rep, typename Period>
inline bool operator>=(std::chrono::duration<Rep, Period> d, std::nullptr_t)
{
    return d.count() >= 0;
}

template<typename Rep, typename Period>
inline bool operator>(std::chrono::duration<Rep, Period> d, std::nullptr_t)
{
    return d.count() > 0;
}

// Convenient inline function for converting std::chrono::duration,
// of a clock with any period, into the classic Posix "struct timeval":
template <class Rep, class Period>
static inline void fill_tv(std::chrono::duration<Rep, Period> d, timeval *tv)
{
    using namespace std::chrono;
    auto sec = duration_cast<seconds>(d);
    auto usec = duration_cast<microseconds>(d - sec);
    tv->tv_sec = sec.count();
    tv->tv_usec = usec.count();
}

#endif /* OSV_CLOCK_HH_ */
