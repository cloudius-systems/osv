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
#include "sched.hh"

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
    u64 time = clock::get()->time();
    auto sec = time / 1000000000;
    auto nsec = time % 1000000000;
    tv->tv_sec = sec;
    tv->tv_usec = nsec / 1000;
    return 0;
}

int nanosleep(const struct timespec* req, struct timespec* rem)
{
    sched::thread::sleep_until(clock::get()->time() + convert(*req));
    return 0;
}

int usleep(useconds_t usec)
{
    sched::thread::sleep_until(clock::get()->time() + usec * 1_us);
    return 0;
}

int clock_gettime(clockid_t clk_id, struct timespec* ts)
{
    switch (clk_id) {
    case CLOCK_REALTIME:
    {
        auto time = clock::get()->time();
        auto sec = time / 1000000000;
        auto nsec = time % 1000000000;
        ts->tv_sec = sec;
        ts->tv_nsec = nsec;
        return 0;
    }
    case CLOCK_MONOTONIC:
    {
        auto t = osv::clock::uptime::now().time_since_epoch();
        ts->tv_sec = std::chrono::duration_cast<std::chrono::seconds>(t).
                        count();
        ts->tv_nsec = std::chrono::duration_cast<std::chrono::nanoseconds>(t).
                        count() % 1000000000;
        return 0;
    }
    default:
        return libc_error(EINVAL);
    }
}

extern "C"
int __clock_gettime(clockid_t clk_id, struct timespec* ts) __attribute__((alias("clock_gettime")));

int clock_getres(clockid_t clk_id, struct timespec* ts)
{
    switch (clk_id) {
    case CLOCK_REALTIME:
    case CLOCK_MONOTONIC:
        if (ts) {
            ts->tv_sec = 0;
            ts->tv_nsec = 1;
        }
        return 0;
    default:
        return libc_error(EINVAL);
    }
}

int clock_getcpuclockid(pid_t pid, clockid_t* clock_id)
{
    return libc_error(ENOSYS);
}

clock_t clock (void)
{
    WARN_STUBBED();
    return -1;
}

NO_SYS(int utime(const char *, const struct utimbuf *));
