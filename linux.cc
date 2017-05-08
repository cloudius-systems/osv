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
#include <osv/stubbing.hh>

#include <syscall.h>
#include <stdarg.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <sys/mman.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/mman.h>

#include <unordered_map>

#include <musl/src/internal/ksigaction.h>

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


// As explained in the sched_getaffinity(2) manual page, the interface of the
// sched_getaffinity() function is slightly different than that of the actual
// system call we need to implement here.
#define __NR_sched_getaffinity_syscall __NR_sched_getaffinity
static int sched_getaffinity_syscall(
        pid_t pid, unsigned len, unsigned long *mask)
{
        int ret = sched_getaffinity(
                pid, len, reinterpret_cast<cpu_set_t *>(mask));
        if (ret == 0) {
            // The Linux system call doesn't zero the entire len bytes of the
            // given mask - it only sets up to the configured maximum number of
            // CPUs (e.g., 64) and returns the amount of bytes it set at mask.
            // We don't have this limitation (our sched_getaffinity() does zero
            // the whole len), but some user code (e.g., libnuma's
            // set_numa_max_cpu()) expect a reasonably low number to be
            // returned, even when len is unrealistically high, so let's
            // return a lower length too.
            ret = std::min(len, sched::max_cpus / 8);
        }
        return ret;
}

// Only void* return value of mmap is type casted, as syscall returns long.
long long_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset) {
    return (long) mmap(addr, length, prot, flags, fd, offset);
}
#define __NR_long_mmap __NR_mmap


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

int rt_sigaction(int sig, const struct k_sigaction * act, struct k_sigaction * oact, size_t sigsetsize)
{
    struct sigaction libc_act, libc_oact, *libc_act_p = nullptr;
    memset(&libc_act, 0, sizeof(libc_act));
    memset(&libc_oact, 0, sizeof(libc_act));

    if (act) {
        libc_act.sa_handler = act->handler;
        libc_act.sa_flags = act->flags & ~SA_RESTORER;
        libc_act.sa_restorer = nullptr;
        memcpy(&libc_act.sa_mask, &act->mask, sizeof(libc_act.sa_mask));
        libc_act_p = &libc_act;
    }

    int ret = sigaction(sig, libc_act_p, &libc_oact);

    if (oact) {
        oact->handler = libc_oact.sa_handler;
        oact->flags = libc_oact.sa_flags;
        oact->restorer = nullptr;
        memcpy(oact->mask, &libc_oact.sa_mask, sizeof(oact->mask));
    }

    return ret;
}

int rt_sigprocmask(int how, sigset_t * nset, sigset_t * oset, size_t sigsetsize)
{
    return sigprocmask(how, nset, oset);
}

#define __NR_sys_exit __NR_exit

static int sys_exit(int ret)
{
    exit(ret);
    return 0;
}

long syscall(long number, ...)
{
    // Save FPU state and restore it at the end of this function
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);

    switch (number) {
    SYSCALL2(open, const char *, int);
    SYSCALL3(read, int, char *, size_t);
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
    SYSCALL3(sched_getaffinity_syscall, pid_t, unsigned, unsigned long *);
    SYSCALL6(long_mmap, void *, size_t, int, int, int, off_t);
    SYSCALL2(munmap, void *, size_t);
    SYSCALL4(rt_sigaction, int, const struct k_sigaction *, struct k_sigaction *, size_t);
    SYSCALL4(rt_sigprocmask, int, sigset_t *, sigset_t *, size_t);
    SYSCALL1(sys_exit, int);
    SYSCALL2(sigaltstack, const stack_t *, stack_t *);
    SYSCALL5(select, int, fd_set *, fd_set *, fd_set *, struct timeval *);
    SYSCALL3(madvise, void *, size_t, int);
    SYSCALL0(sched_yield);
    SYSCALL3(mincore, void *, size_t, unsigned char *);
    SYSCALL3(dup3, int, int, int);
    }

    debug_always("syscall(): unimplemented system call %d\n", number);
    errno = ENOSYS;
    return -1;
}
long __syscall(long number, ...)  __attribute__((alias("syscall")));

extern "C" long syscall_wrapper(long number, ...)
{
    int errno_backup = errno;
    // syscall and function return value are in rax
    auto ret = syscall(number);
    int result = -errno;
    errno = errno_backup;
    if (ret < 0 && ret >= -4096) {
	return result;
    }
    return ret;
}
