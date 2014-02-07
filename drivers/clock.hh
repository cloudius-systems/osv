/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CLOCK_HH_
#define CLOCK_HH_

#include <osv/types.h>

/**
 * OSv low-level time-keeping interface
 *
 * This is an abstract class providing an interface to OSv's basic
 * time-keeping functions.
 *
 * clock::get() returns the single concrete instance of this interface,
 * which may be a kvmclock, xenclock, or hpetclock - depending on which
 * of these the hypervisor provides. This clock instance may then be
 * queried for the current time - for example, clock::get()->time().
 *
 * The methods of this class are not type-safe, in that they return times
 * as unadorned integers (s64) whose type does not specify the time units
 * or the epoch. Prefer instead to use the types from <osv/clock.hh>:
 * osv::clock::monotonic et al. Their efficiency is identical to the
 * methods of this class, but they allow much better compile-time checking.
 */
class clock {
public:
    virtual ~clock();
    static void register_clock(clock* c);
    /**
     * Get a pointer to the single concrete instance of the clock class.
     *
     * This instance can then be used to query the current time.
     * For example, clock::get()->time().
     *
     * This function always returns the same clock, which OSv considers the
     * best and most efficient way to query the time on the this hypervisor.
     * On KVM and Xen, it can be kvmclock or xenclock respectively, which are
     * very efficient para-virtual clocks. As a last resort, it defaults to
     * hpetclock.
     * \return A pointer to the concrete instance of the clock class.
     */
    static clock* get() __attribute__((no_instrument_function));
    /**
     * Get the current value of the nanosecond-resolution uptime clock.
     *
     * The uptime clock is a *monotonic* clock, which can only go forward.
     * It measures, in nanoseconds, the time that has passed since OSv's boot,
     * and is usually used to measure time intervals.
     *
     * For improved type safety, it is recommended to use
     * osv::clock::uptime::now() instead of clock::get()->uptime()
     *
     * Note that uptime() may remain at 0 without change for some time during
     * the very early stages of the boot process.
     */
    virtual s64 uptime() = 0;
    /**
     * Get the current nanosecond-resolution wall-clock time.
     *
     * The wall-clock time is what humans normally think of as "the time", and
     * Posix calls the "realtime clock". It is returned as the number of
     * nanoseconds since the Unix epoch (midnight UTC, Jan 1st, 1970).
     *
     * For improved type safety, it is recommended to use
     * osv::clock::wall::now() instead of clock::get()->time()
     *
     * This clock may be influenced by gradual or discontinuous adjustments
     * to the time, made by an administrator or software like NTP. It can not
     * even be assumed to be monotonic. In OSv, time adjustments are not
     * supported in the guest (we do not offer a set_time() function), but
     * when a paravirtual clock is available (kvmclock or xenclock), time
     * adjustments made in the host (e.g., NTP running in the host) are made
     * visible to the guest.
     *
     */
    virtual s64 time() = 0;
    /*
     * Return current estimate of wall-clock time at OSV's boot.
     *
     * Note that this is not necessarily constant. Rather, if we (or more
     * likely, the host) adjust the wall-clock time, boot_time() will be
     * adjusted so that always boot_time() + uptime() = time().
     *
     * In other words, boot_time() is the instantaneous value of
     * time()-uptime(), but faster to calculate and doesn't suffer from
     * the above expression evaluating time() and uptime() in slightly
     * different times.
     */
    virtual s64 boot_time() = 0;

    /*
     * convert a processor based timestamp (x86 tsc's for instance) to nanoseconds.
     *
     * Not all clocks are required to implement it.
     */
    virtual u64 processor_to_nano(u64 ticks) { return 0; }
private:
    static clock* _c;
};
#endif /* CLOCK_HH_ */
