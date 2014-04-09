/*
 * Copyright (C) 2013-2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// linux syscalls

#include <osv/debug.hh>
#include <boost/format.hpp>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/waitqueue.hh>

#include <syscall.h>
#include <stdarg.h>
#include <time.h>

#include <unordered_map>

long gettid()
{
    return sched::thread::current()->id();
}

// We don't expect applications to use the Linux futex() system call (it is
// normally only used to implement higher-level synchronization mechanisms),
// but unfortunately gcc's C++ runtime uses a subset of futex in the
// __cxa__guard_* functions, which safeguard the concurrent initialization
// of function-scope static objects. We only implement here this subset.
// The __cxa_guard_* functions only call futex in the rare case of contention,
// in fact so rarely that OSv existed for a year before anyone noticed futex
// was missing. So the performance of this implementation is not critical.
static std::unordered_map<void*, waitqueue> queues;
static mutex queues_mutex;
enum {
    FUTEX_WAIT = 0,
    FUTEX_WAKE = 1,
};

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
        int *uaddr2, int val3)
{
    switch (op) {
    case FUTEX_WAIT:
        assert(timeout == 0);
        WITH_LOCK(queues_mutex) {
            if (*uaddr == val) {
                waitqueue &q = queues[uaddr];
                q.wait(queues_mutex);
            }
        }
        return 0;
    case FUTEX_WAKE:
        assert(val == INT_MAX);
        WITH_LOCK(queues_mutex) {
            auto i = queues.find(uaddr);
            if (i != queues.end()) {
                i->second.wake_all(queues_mutex);
                queues.erase(i);
            }
        }
        // FIXME: We are expected to return a count of woken threads, but
        // wake_all doesn't have this feature, and the only user we care
        // about, __cxa_guard_*, doesn't need this return value anyway.
        return 0;
    default:
        abort("Unimplemented futex() operation %d\n", op);
    }
}


long syscall(long number, ...)
{
    switch (number) {
    case __NR_write: {
        va_list args;
        int arg1;
        const void *arg2;
        size_t arg3;
        va_start(args, number);
        arg1 = va_arg(args, typeof(arg1));
        arg2 = va_arg(args, typeof(arg2));
        arg3 = va_arg(args, typeof(arg3));
        va_end(args);

        return write(arg1, arg2, arg3);
    }
    case __NR_gettid: return gettid();
    case __NR_clock_gettime: {
        va_list args;
        clockid_t arg1;
        struct timespec *arg2;
        va_start(args, number);
        arg1 = va_arg(args, typeof(arg1));
        arg2 = va_arg(args, typeof(arg2));
        va_end(args);
        return clock_gettime(arg1, arg2);
        }
    case __NR_clock_getres: {
        va_list args;
        clockid_t arg1;
        struct timespec *arg2;
        va_start(args, number);
        arg1 = va_arg(args, typeof(arg1));
        arg2 = va_arg(args, typeof(arg2));
        va_end(args);
        return clock_getres(arg1, arg2);
        }
    case __NR_futex: {
        va_list args;
        int *arg1;
        int arg2;
        int arg3;
        const struct timespec *arg4;
        int *arg5;
        int arg6;
        va_start(args, number);
        arg1 = va_arg(args, typeof(arg1));
        arg2 = va_arg(args, typeof(arg2));
        arg3 = va_arg(args, typeof(arg3));
        arg4 = va_arg(args, typeof(arg4));
        arg5 = va_arg(args, typeof(arg5));
        arg6 = va_arg(args, typeof(arg6));
        va_end(args);
        return futex(arg1, arg2, arg3, arg4, arg5, arg6);
    }
    }

    abort("syscall(): unimplemented system call %d. Aborting.\n", number);
}
