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
#include <memory>

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
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/unistd.h>
#include <sys/random.h>

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

static long set_mempolicy(int policy, unsigned long *nmask,
        unsigned long maxnode)
{
    // OSv has very minimal support for NUMA - merely exposes
    // all cpus as a single node0 and cannot really apply any meaningful policy
    // Therefore we implement this as noop, ignore all arguments and return success
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

#define __NR_sched_setaffinity_syscall __NR_sched_setaffinity
static int sched_setaffinity_syscall(
        pid_t pid, unsigned len, unsigned long *mask)
{
    return sched_setaffinity(
            pid, len, reinterpret_cast<cpu_set_t *>(mask));
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

#define __NR_sys_exit_group __NR_exit_group
static int sys_exit_group(int ret)
{
    exit(ret);
    return 0;
}

#define __NR_sys_ioctl __NR_ioctl
//
// We need to define explicit sys_ioctl that takes these 3 parameters to conform
// to Linux signature of this system call. The underlying ioctl function which we delegate to
// is variadic and takes slightly different paremeters and therefore cannot be used directly
// as other system call implementations can.
static int sys_ioctl(unsigned int fd, unsigned int command, unsigned long arg)
{
    return ioctl(fd, command, arg);
}

static int pselect6(int nfds, fd_set *readfds, fd_set *writefds,
                   fd_set *exceptfds, const struct timespec *timeout_ts,
                   void *sig)
{
    // As explained in the pselect(2) manual page, the system call pselect accepts
    // pointer to a structure holding pointer to sigset_t and its size which is different
    // the glibc version of pselect(). For now we are delaying implementation of this call
    // scenario and raising an error when such call happens.
    if(sig) {
        WARN_ONCE("pselect6(): unimplemented with not-null sigmask\n");
        errno = ENOSYS;
        return -1;
    }

    return pselect(nfds, readfds, writefds, exceptfds, timeout_ts, NULL);
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
    SYSCALL3(connect, int, struct sockaddr *, socklen_t);
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
    SYSCALL4(openat, int, const char *, int, mode_t);
    SYSCALL3(socket, int, int, int);
    SYSCALL5(setsockopt, int, int, int, char *, int);
    SYSCALL5(getsockopt, int, int, int, char *, unsigned int *);
    SYSCALL3(getpeername, int, struct sockaddr *, unsigned int *);
    SYSCALL3(bind, int, struct sockaddr *, int);
    SYSCALL2(listen, int, int);
    SYSCALL3(sys_ioctl, unsigned int, unsigned int, unsigned long);
    SYSCALL2(stat, const char *, struct stat *);
    SYSCALL2(fstat, int, struct stat *);
    SYSCALL3(getsockname, int, struct sockaddr *, socklen_t *);
    SYSCALL6(sendto, int, const void *, size_t, int, const struct sockaddr *, socklen_t);
    SYSCALL3(sendmsg, int, const struct msghdr *, int);
    SYSCALL6(recvfrom, int, void *, size_t, int, struct sockaddr *, socklen_t *);
    SYSCALL3(recvmsg, int, struct msghdr *, int);
    SYSCALL3(dup3, int, int, int);
    SYSCALL2(flock, int, int);
    SYSCALL4(pwrite64, int, const void *, size_t, off_t);
    SYSCALL1(fdatasync, int);
    SYSCALL6(pselect6, int, fd_set *, fd_set *, fd_set *, const struct timespec *, void *);
    SYSCALL3(fcntl, int, int, int);
    SYSCALL4(pread64, int, void *, size_t, off_t);
    SYSCALL2(ftruncate, int, off_t);
    SYSCALL1(fsync, int);
    SYSCALL5(epoll_pwait, int, struct epoll_event *, int, int, const sigset_t*);
    SYSCALL3(getrandom, char *, size_t, unsigned int);
    SYSCALL2(nanosleep, const struct timespec*, struct timespec *);
    SYSCALL4(fstatat, int, const char *, struct stat *, int);
    SYSCALL1(sys_exit_group, int);
    SYSCALL4(readlinkat, int, const char *, char *, size_t);
    SYSCALL0(getpid);
    SYSCALL3(set_mempolicy, int, unsigned long *, unsigned long);
    SYSCALL3(sched_setaffinity_syscall, pid_t, unsigned, unsigned long *);
    }

    debug_always("syscall(): unimplemented system call %d\n", number);
    errno = ENOSYS;
    return -1;
}
long __syscall(long number, ...)  __attribute__((alias("syscall")));

// In x86-64, a SYSCALL instruction has exactly 6 parameters, because this is the number of registers
// alloted for passing them (additional parameters *cannot* be passed on the stack). So we can get
// 7 arguments to this function (syscall number plus its 6 parameters). Because in the x86-64 ABI the
// seventh argument is on the stack, we must pass the arguments explicitly to the syscall() function
// and can't just call it without any arguments and hope everything will be passed on
extern "C" long syscall_wrapper(long number, long p1, long p2, long p3, long p4, long p5, long p6)
{
    int errno_backup = errno;
    // syscall and function return value are in rax
    auto ret = syscall(number, p1, p2, p3, p4, p5, p6);
    int result = -errno;
    errno = errno_backup;
    if (ret < 0 && ret >= -4096) {
        return result;
    }
    return ret;
}
