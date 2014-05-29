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

u64 convert(const timespec& ts)
{
    return ts.tv_sec * 1000000000 + ts.tv_nsec;
}

extern "C"
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

int nanosleep(const struct timespec* req, struct timespec* rem)
{
    sched::thread::sleep(std::chrono::nanoseconds(convert(*req)));
    return 0;
}

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

// Return the total amount of cpu time used by the process. This is the amount
// of time that passed since boot multiplied by the number of CPUs, from which
// we subtract the time spent in the idle threads.
// Besides the idle thread, we do not currently account for "steal time",
// i.e., time in which the hypervisor preempted us and ran other things.
// In other words, when a hypervisor gives us only a part of a CPU, we pretend
// it is still a full CPU, just a slower one. Ordinary CPUs behave similarly
// when faced with variable-speed CPUs.
static osv::clock::uptime::duration process_cputime()
{
    // FIXME: This code does not handle the possibility of CPU hot-plugging.
    // See issue #152 for a suggested solution.
    auto ret = osv::clock::uptime::now().time_since_epoch();
    ret *= sched::cpus.size();
    for (sched::cpu *cpu : sched::cpus) {
        ret -= cpu->idle_thread->thread_clock();
    }
    // Currently, idle_thread->thread_clock() isn't updated while that thread
    // is running, which means that in the middle of idle time-slices, we
    // think this time-slice has been busy. We "cover up" this error by
    // monotonizing the return value. This is good enough when idle time-
    // slices are relatively short (e.g., always interrupted by the load
    // balancer waking up). In the future we should have a better fix.
    static std::atomic<osv::clock::uptime::duration> lastret;
    auto l = lastret.load(std::memory_order_relaxed);
    while (ret > l &&
           !lastret.compare_exchange_weak(l, ret, std::memory_order_relaxed));
    if (ret < l) {
        ret = l;
    }
    return ret;
}

int clock_gettime(clockid_t clk_id, struct timespec* ts)
{
    switch (clk_id) {
    case CLOCK_MONOTONIC:
        fill_ts(osv::clock::uptime::now().time_since_epoch(), ts);
        break;
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
        fill_ts(osv::clock::wall::now().time_since_epoch(), ts);
        break;
    case CLOCK_PROCESS_CPUTIME_ID:
        fill_ts(process_cputime(), ts);
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

extern "C"
int __clock_gettime(clockid_t clk_id, struct timespec* ts) __attribute__((alias("clock_gettime")));

int clock_getres(clockid_t clk_id, struct timespec* ts)
{
    switch (clk_id) {
    case CLOCK_REALTIME:
    case CLOCK_REALTIME_COARSE:
    case CLOCK_PROCESS_CPUTIME_ID:
    case CLOCK_THREAD_CPUTIME_ID:
    case CLOCK_MONOTONIC:
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

int clock_getcpuclockid(pid_t pid, clockid_t* clock_id)
{
    return CLOCK_PROCESS_CPUTIME_ID;
}

clock_t clock(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &ts);
    return ts.tv_sec * 1000000000L + ts.tv_nsec;
}

NO_SYS(int utime(const char *, const struct utimbuf *));
