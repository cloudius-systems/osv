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
#include <errno.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/utsname.h>

#include <unordered_map>

extern "C" long gettid()
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
    FUTEX_WAIT           = 0,
    FUTEX_WAKE           = 1,
    FUTEX_PRIVATE_FLAG   = 128,
    FUTEX_CLOCK_REALTIME = 256,
    FUTEX_CMD_MASK       = ~(FUTEX_PRIVATE_FLAG|FUTEX_CLOCK_REALTIME),
};

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
        int *uaddr2, int val3)
{
    switch (op & FUTEX_CMD_MASK) {
    case FUTEX_WAIT:
        WITH_LOCK(queues_mutex) {
            if (*uaddr == val) {
                waitqueue &q = queues[uaddr];
                if (timeout) {
                    sched::timer tmr(*sched::thread::current());
                    tmr.set(std::chrono::seconds(timeout->tv_sec) +
                            std::chrono::nanoseconds(timeout->tv_nsec));
                    sched::thread::wait_for(queues_mutex, tmr, q);
                    // FIXME: testing if tmr was expired isn't quite right -
                    // we could have had both a wakeup and timer expiration
                    // racing. It would be more correct to check if we were
                    // waken by a FUTEX_WAKE. But how?
                    if (tmr.expired()) {
                        errno = ETIMEDOUT;
                        return -1;
                    }
                } else {
                    q.wait(queues_mutex);
                }
                return 0;
            } else {
                errno = EWOULDBLOCK;
                return -1;
            }
        }
    case FUTEX_WAKE:
        if(val < 0) {
            errno = EINVAL;
            return -1;
        }

        WITH_LOCK(queues_mutex) {
            auto i = queues.find(uaddr);
            if (i != queues.end()) {
                int waken = 0;
                while( (val > waken) && !(i->second.empty()) ) {
                    i->second.wake_one(queues_mutex);
                    waken++;
                }
                if(i->second.empty()) {
                    queues.erase(i);
                }
                return waken;
            }
        }
        return 0;
    default:
        abort("Unimplemented futex() operation %d\n", op);
    }
}

// We're not supposed to export the get_mempolicy() function, as this
// function is not part of glibc (which OSv emulates), but part of a
// separate library libnuma, which the user can simply load. libnuma's
// implementation of get_mempolicy() calls syscall(__NR_get_mempolicy,...),
// so this is what we need to expose, below.

#define MPOL_DEFAULT 0
#define MPOL_F_NODE         (1<<0)
#define MPOL_F_ADDR         (1<<1)
#define MPOL_F_MEMS_ALLOWED (1<<2)

static long get_mempolicy(int *policy, unsigned long *nmask,
        unsigned long maxnode, void *addr, int flags)
{
    // As OSv has no support for NUMA nodes, we do here the minimum possible,
    // which is basically to return the same policy (MPOL_DEFAULT) and list
    // of nodes (just node 0) no matter if the caller asked for the default
    // policy, the allowed policy, or the policy for a specific address.
    if ((flags & MPOL_F_NODE)) {
        *policy = 0; // in this case, store a node id, not a policy
        return 0;
    }
    if (policy) {
        *policy = MPOL_DEFAULT;
    }
    if (nmask) {
        if (maxnode < 1) {
            errno = EINVAL;
            return -1;
        }
        nmask[0] |= 1;
    }
    return 0;
}


#define SYSCALL0(fn) case (__NR_##fn): return fn()

#define SYSCALL1(fn, __t1)                  \
        case (__NR_##fn): do {              \
        va_list args;                       \
        __t1 arg1;                          \
        va_start(args, number);             \
        arg1 = va_arg(args, __t1);          \
        va_end(args);                       \
        return fn(arg1);                    \
        } while (0)

#define SYSCALL2(fn, __t1, __t2)            \
        case (__NR_##fn): do {              \
        va_list args;                       \
        __t1 arg1;                          \
        __t2 arg2;                          \
        va_start(args, number);             \
        arg1 = va_arg(args, __t1);          \
        arg2 = va_arg(args, __t2);          \
        va_end(args);                       \
        return fn(arg1, arg2);              \
        } while (0)

#define SYSCALL3(fn, __t1, __t2, __t3)          \
        case (__NR_##fn): do {                  \
        va_list args;                           \
        __t1 arg1;                              \
        __t2 arg2;                              \
        __t3 arg3;                              \
        va_start(args, number);                 \
        arg1 = va_arg(args, __t1);              \
        arg2 = va_arg(args, __t2);              \
        arg3 = va_arg(args, __t3);              \
        va_end(args);                           \
        return fn(arg1, arg2, arg3);            \
        } while (0)

#define SYSCALL4(fn, __t1, __t2, __t3, __t4)    \
        case (__NR_##fn): do {                  \
        va_list args;                           \
        __t1 arg1;                              \
        __t2 arg2;                              \
        __t3 arg3;                              \
        __t4 arg4;                              \
        va_start(args, number);                 \
        arg1 = va_arg(args, __t1);              \
        arg2 = va_arg(args, __t2);              \
        arg3 = va_arg(args, __t3);              \
        arg4 = va_arg(args, __t4);              \
        va_end(args);                           \
        return fn(arg1, arg2, arg3, arg4);      \
        } while (0)

#define SYSCALL5(fn, __t1, __t2, __t3, __t4, __t5)    \
        case (__NR_##fn): do {                  \
        va_list args;                           \
        __t1 arg1;                              \
        __t2 arg2;                              \
        __t3 arg3;                              \
        __t4 arg4;                              \
        __t5 arg5;                              \
        va_start(args, number);                 \
        arg1 = va_arg(args, __t1);              \
        arg2 = va_arg(args, __t2);              \
        arg3 = va_arg(args, __t3);              \
        arg4 = va_arg(args, __t4);              \
        arg5 = va_arg(args, __t5);              \
        va_end(args);                           \
        return fn(arg1, arg2, arg3, arg4, arg5);\
        } while (0)

#define SYSCALL6(fn, __t1, __t2, __t3, __t4, __t5, __t6)        \
        case (__NR_##fn): do {                                  \
        va_list args;                                           \
        __t1 arg1;                                              \
        __t2 arg2;                                              \
        __t3 arg3;                                              \
        __t4 arg4;                                              \
        __t5 arg5;                                              \
        __t6 arg6;                                              \
        va_start(args, number);                                 \
        arg1 = va_arg(args, __t1);                              \
        arg2 = va_arg(args, __t2);                              \
        arg3 = va_arg(args, __t3);                              \
        arg4 = va_arg(args, __t4);                              \
        arg5 = va_arg(args, __t5);                              \
        arg6 = va_arg(args, __t6);                              \
        va_end(args);                                           \
        return fn(arg1, arg2, arg3, arg4, arg5, arg6);          \
        } while (0)


long syscall(long number, ...)
{
    switch (number) {
    SYSCALL1(uname, struct utsname *);
    SYSCALL3(write, int, const void *, size_t);
    SYSCALL0(gettid);
    SYSCALL2(clock_gettime, clockid_t, struct timespec *);
    SYSCALL2(clock_getres, clockid_t, struct timespec *);
    SYSCALL6(futex, int *, int, int, const struct timespec *, int *, int);
    SYSCALL1(close, int);
    SYSCALL2(pipe2, int *, int);
    SYSCALL1(epoll_create1, int);
    SYSCALL2(eventfd2, unsigned int, int);
    SYSCALL4(epoll_ctl, int, int, int, struct epoll_event *);
    SYSCALL4(epoll_wait, int, struct epoll_event *, int, int);
    SYSCALL4(accept4, int, struct sockaddr *, socklen_t *, int);
    SYSCALL5(get_mempolicy, int *, unsigned long *, unsigned long, void *, int);
    }

    abort("syscall(): unimplemented system call %d. Aborting.\n", number);
}
long __syscall(long number, ...)  __attribute__((alias("syscall")));
