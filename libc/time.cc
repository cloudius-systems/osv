/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <api/utime.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>
#include <osv/stubbing.hh>
#include "libc.hh"
#include <osv/clock.hh>
#include <osv/sched.hh>
#include "pthread.hh"

u64 convert(const timespec& ts)
{
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

extern "C" OSV_LIBC_API
int gettimeofday(struct timeval* tv, struct timezone* tz)
{
    if (!tv) {
        return 0;
    }
    using namespace std::chrono;
    auto d = osv::clock::wall::now().time_since_epoch();
    tv->tv_sec = duration_cast<seconds>(d).count();
    tv->tv_usec = duration_cast<microseconds>(d).count() % 1000000;
    return 0;
}

OSV_LIBC_API
int nanosleep(const struct timespec* req, struct timespec* rem)
{
    sched::thread::sleep(std::chrono::nanoseconds(convert(*req)));
    return 0;
}

OSV_LIBC_API
int clock_nanosleep(clockid_t clock_id, int flags,
                    const struct timespec *request,
                    struct timespec *remain)
{
    //We ignore the "remain" argument same way we do it above in nanosleep()
    //This argument is only relevant if the "sleeping" thread is interrupted
    //by signals. But OSv signal implementation is limited and would not allow
    //for such a scenario and both nanosleep() and clock_nanosleep() would
    //never return EINTR
    if (flags || clock_id != CLOCK_REALTIME) {
        return ENOTSUP;
    }
    sched::thread::sleep(std::chrono::nanoseconds(convert(*request)));
    return 0;
}

OSV_LIBC_API
int usleep(useconds_t usec)
{
    sched::thread::sleep(std::chrono::microseconds(usec));
    return 0;
}

// Convenient inline function for converting std::chrono::duration,
// of a clock with any period, into the classic Posix "struct timespec":
template <class Rep, class Period>
static inline void fill_ts(std::chrono::duration<Rep, Period> d, timespec *ts)
{
    using namespace std::chrono;
    ts->tv_sec = duration_cast<seconds>(d).count();
    ts->tv_nsec = duration_cast<nanoseconds>(d).count() % 1000000000;
}

OSV_LIBC_API
int clock_gettime(clockid_t clk_id, struct timespec* ts)
{
    switch (clk_id) {
    case CLOCK_BOOTTIME:
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_MONOTONIC_RAW:
        fill_ts(osv::clock::uptime::now().time_since_epoch(), ts);
        break;
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        fill_ts(osv::clock::wall::now().time_since_epoch(), ts);
        break;
    case CLOCK_PROCESS_CPUTIME_ID:
        fill_ts(sched::process_cputime(), ts);
        break;
    case CLOCK_THREAD_CPUTIME_ID:
        fill_ts(sched::thread::current()->thread_clock(), ts);
        break;

    default:
        if (clk_id < _OSV_CLOCK_SLOTS) {
            return libc_error(EINVAL);
        } else {
            auto thread = sched::thread::find_by_id(clk_id - _OSV_CLOCK_SLOTS);
            fill_ts(thread->thread_clock(), ts);
        }
    }

    return 0;
}

extern "C" OSV_LIBC_API
int __clock_gettime(clockid_t clk_id, struct timespec* ts) __attribute__((alias("clock_gettime")));

OSV_LIBC_API
int clock_getres(clockid_t clk_id, struct timespec* ts)
{
    switch (clk_id) {
    case CLOCK_BOOTTIME:
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
    case CLOCK_MONOTONIC:
    case CLOCK_MONOTONIC_COARSE:
    case CLOCK_MONOTONIC_RAW:
        break;
    default:
        if (clk_id < _OSV_CLOCK_SLOTS) {
            return libc_error(EINVAL);
        }
    }

    if (ts) {
        ts->tv_sec = 0;
        ts->tv_nsec = 1;
    }
    return 0;
}

OSV_LIBC_API
int clock_getcpuclockid(pid_t pid, clockid_t* clock_id)
{
    return CLOCK_PROCESS_CPUTIME_ID;
}

OSV_LIBC_API
clock_t clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}
