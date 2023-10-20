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
#include <osv/export.h>
#include <osv/trace.hh>
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
#include <sys/statx.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/unistd.h>
#include <sys/random.h>
#include <sys/vfs.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/sysinfo.h>
#include <sys/sendfile.h>
#include <sys/prctl.h>
#include <sys/timerfd.h>
#include <sys/resource.h>
#include <termios.h>
#include <poll.h>

#include <unordered_map>

#include <musl/src/internal/ksigaction.h>

extern "C" int eventfd2(unsigned int, int);

extern "C" OSV_LIBC_API long gettid()
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
    FUTEX_WAIT_BITSET    = 9,
    FUTEX_PRIVATE_FLAG   = 128,
    FUTEX_CLOCK_REALTIME = 256,
    FUTEX_CMD_MASK       = ~(FUTEX_PRIVATE_FLAG|FUTEX_CLOCK_REALTIME),
};

#define FUTEX_BITSET_MATCH_ANY  0xffffffff

int futex(int *uaddr, int op, int val, const struct timespec *timeout,
        int *uaddr2, uint32_t val3)
{
    switch (op & FUTEX_CMD_MASK) {
    case FUTEX_WAIT_BITSET:
        if (val3 != FUTEX_BITSET_MATCH_ANY) {
            abort("Unimplemented futex() operation %d\n", op);
        }

    case FUTEX_WAIT:
        WITH_LOCK(queues_mutex) {
            if (*uaddr == val) {
                waitqueue &q = queues[uaddr];
                if (timeout) {
                    sched::timer tmr(*sched::thread::current());
                    if ((op & FUTEX_CMD_MASK) == FUTEX_WAIT_BITSET) {
                        // If FUTEX_WAIT_BITSET we need to interpret timeout as an absolute
                        // time point. If futex operation FUTEX_CLOCK_REALTIME is set we will use
                        // real-time clock otherwise we will use monotonic clock
                        if (op & FUTEX_CLOCK_REALTIME) {
                            tmr.set(osv::clock::wall::time_point(std::chrono::seconds(timeout->tv_sec) +
                                                                 std::chrono::nanoseconds(timeout->tv_nsec)));
                        } else {
                            tmr.set(osv::clock::uptime::time_point(std::chrono::seconds(timeout->tv_sec) +
                                                                   std::chrono::nanoseconds(timeout->tv_nsec)));
                        }
                    } else {
                        tmr.set(std::chrono::seconds(timeout->tv_sec) +
                                std::chrono::nanoseconds(timeout->tv_nsec));
                    }
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
#define __NR_sys_sched_getaffinity __NR_sched_getaffinity
static int sys_sched_getaffinity(
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

#define __NR_sys_sched_setaffinity __NR_sched_setaffinity
static int sys_sched_setaffinity(
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


#define SYSCALL0(fn) case (__NR_##fn): do { long ret = fn(); trace_syscall_##fn(ret); return ret; } while (0)

#define SYSCALL1(fn, __t1)             \
        case (__NR_##fn): do {         \
        va_list args;                  \
        __t1 arg1;                     \
        va_start(args, number);        \
        arg1 = va_arg(args, __t1);     \
        va_end(args);                  \
        auto ret = fn(arg1);           \
        trace_syscall_##fn(ret, arg1); \
        return ret;                    \
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
        auto ret = fn(arg1, arg2);          \
        trace_syscall_##fn(ret, arg1, arg2);\
        return ret;                         \
        } while (0)

#define SYSCALL3(fn, __t1, __t2, __t3)             \
        case (__NR_##fn): do {                     \
        va_list args;                              \
        __t1 arg1;                                 \
        __t2 arg2;                                 \
        __t3 arg3;                                 \
        va_start(args, number);                    \
        arg1 = va_arg(args, __t1);                 \
        arg2 = va_arg(args, __t2);                 \
        arg3 = va_arg(args, __t3);                 \
        va_end(args);                              \
        auto ret = fn(arg1, arg2, arg3);           \
        trace_syscall_##fn(ret, arg1, arg2, arg3); \
        return ret;                                \
        } while (0)

#define SYSCALL4(fn, __t1, __t2, __t3, __t4)             \
        case (__NR_##fn): do {                           \
        va_list args;                                    \
        __t1 arg1;                                       \
        __t2 arg2;                                       \
        __t3 arg3;                                       \
        __t4 arg4;                                       \
        va_start(args, number);                          \
        arg1 = va_arg(args, __t1);                       \
        arg2 = va_arg(args, __t2);                       \
        arg3 = va_arg(args, __t3);                       \
        arg4 = va_arg(args, __t4);                       \
        va_end(args);                                    \
        auto ret = fn(arg1, arg2, arg3, arg4);           \
        trace_syscall_##fn(ret, arg1, arg2, arg3, arg4); \
        return ret;                                      \
        } while (0)

#define SYSCALL5(fn, __t1, __t2, __t3, __t4, __t5)             \
        case (__NR_##fn): do {                                 \
        va_list args;                                          \
        __t1 arg1;                                             \
        __t2 arg2;                                             \
        __t3 arg3;                                             \
        __t4 arg4;                                             \
        __t5 arg5;                                             \
        va_start(args, number);                                \
        arg1 = va_arg(args, __t1);                             \
        arg2 = va_arg(args, __t2);                             \
        arg3 = va_arg(args, __t3);                             \
        arg4 = va_arg(args, __t4);                             \
        arg5 = va_arg(args, __t5);                             \
        va_end(args);                                          \
        auto ret = fn(arg1, arg2, arg3, arg4, arg5);           \
        trace_syscall_##fn(ret, arg1, arg2, arg3, arg4, arg5); \
        return ret;                                            \
        } while (0)

#define SYSCALL6(fn, __t1, __t2, __t3, __t4, __t5, __t6)             \
        case (__NR_##fn): do {                                       \
        va_list args;                                                \
        __t1 arg1;                                                   \
        __t2 arg2;                                                   \
        __t3 arg3;                                                   \
        __t4 arg4;                                                   \
        __t5 arg5;                                                   \
        __t6 arg6;                                                   \
        va_start(args, number);                                      \
        arg1 = va_arg(args, __t1);                                   \
        arg2 = va_arg(args, __t2);                                   \
        arg3 = va_arg(args, __t3);                                   \
        arg4 = va_arg(args, __t4);                                   \
        arg5 = va_arg(args, __t5);                                   \
        arg6 = va_arg(args, __t6);                                   \
        va_end(args);                                                \
        auto ret = fn(arg1, arg2, arg3, arg4, arg5, arg6);           \
        trace_syscall_##fn(ret, arg1, arg2, arg3, arg4, arg5, arg6); \
        return ret;                                                  \
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
    sched::thread::current()->exit();
    return 0;
}

#define __NR_sys_exit_group __NR_exit_group
static int sys_exit_group(int ret)
{
    exit(ret);
    return 0;
}

#define __NR_sys_getcwd __NR_getcwd
static long sys_getcwd(char *buf, unsigned long size)
{
    if (!buf) {
        errno = EINVAL;
        return -1;
    }
    auto ret = getcwd(buf, size);
    if (!ret) {
        return -1;
    }
    return strlen(ret) + 1;
}

#define __NR_sys_getcpu __NR_getcpu
static long sys_getcpu(unsigned int *cpu, unsigned int *node, void *tcache)
{
    if (cpu) {
        *cpu = sched::cpu::current()->id;
    }

    if (node) {
       *node = 0;
    }

    return 0;
}

#define __NR_sys_ioctl __NR_ioctl
//
// We need to define explicit sys_ioctl that takes these 3 parameters to conform
// to Linux signature of this system call. The underlying ioctl function which we delegate to
// is variadic and takes slightly different paremeters and therefore cannot be used directly
// as other system call implementations can.
#define KERNEL_NCCS 19

// This structure is exactly what glibc expects to receive when calling ioctl()
// with TCGET and is defined in sysdeps/unix/sysv/linux/kernel_termios.h.
struct __kernel_termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[KERNEL_NCCS];
};

static int sys_ioctl(unsigned int fd, unsigned int command, unsigned long arg)
{
    if (command == TCGETS) {
       //The termios structure is slightly different from the version of it used
       //by the syscall so let us translate it manually
       termios _termios;
       auto ret = ioctl(fd, command, &_termios);
       if (!ret) {
           __kernel_termios *ktermios = reinterpret_cast<__kernel_termios*>(arg);
           ktermios->c_iflag = _termios.c_iflag;
           ktermios->c_oflag = _termios.c_oflag;
           ktermios->c_cflag = _termios.c_cflag;
           ktermios->c_lflag = _termios.c_lflag;
           ktermios->c_line = _termios.c_line;
           memcpy(&ktermios->c_cc[0], &_termios.c_cc[0], KERNEL_NCCS * sizeof (cc_t));
       }
       return ret;
    } else {
       return ioctl(fd, command, arg);
    }
}

struct sys_sigset {
    const sigset_t *ss;     /* Pointer to signal set */
    size_t          ss_len; /* Size (in bytes) of object pointed to by 'ss' */
};

static int pselect6(int nfds, fd_set *readfds, fd_set *writefds,
                   fd_set *exceptfds, struct timespec *timeout_ts,
                   sys_sigset* sigmask)
{
    // As explained in the pselect(2) manual page, the system call pselect accepts
    // pointer to a structure holding pointer to sigset_t and its size which is different
    // from the glibc version of pselect().
    // On top of this, the Linux pselect6() system call modifies its timeout argument
    // unlike the glibc pselect() function. Our implementation below is to great extent
    // similar to that of pselect() in core/select.cc
    sigset_t origmask;
    struct timeval timeout;

    if (timeout_ts) {
        timeout.tv_sec = timeout_ts->tv_sec;
        timeout.tv_usec = timeout_ts->tv_nsec / 1000;
    }

    if (sigmask) {
        sigprocmask(SIG_SETMASK, sigmask->ss, &origmask);
    }

    auto ret = select(nfds, readfds, writefds, exceptfds,
                                        timeout_ts == NULL? NULL : &timeout);
    if (sigmask) {
        sigprocmask(SIG_SETMASK, &origmask, NULL);
    }

    if (timeout_ts) {
        timeout_ts->tv_sec = timeout.tv_sec;
        timeout_ts->tv_nsec = timeout.tv_usec * 1000;
    }
    return ret;
}

static int tgkill(int tgid, int tid, int sig)
{
    //
    // Given OSv supports sigle process only, we only support this syscall
    // when thread group id is self (getpid()) or -1 (see https://linux.die.net/man/2/tgkill)
    // AND tid points to the current thread (caller)
    // Ideally we would want to delegate to pthread_kill() but there is no
    // easy way to map tgid to pthread_t so we directly delegate to kill().
    if ((tgid == -1 || tgid == getpid()) && (tid == gettid())) {
        return kill(tgid, sig);
    }

    errno = ENOSYS;
    return -1;
}

#define __NR_sys_getdents64 __NR_getdents64
extern "C" ssize_t sys_getdents64(int fd, void *dirp, size_t count);

#define __NR_sys_brk __NR_brk
void *get_program_break();
static long sys_brk(void *addr)
{
    // The brk syscall is almost the same as the brk() function
    // except it needs to return new program break on success
    // and old one on failure
    void *old_break = get_program_break();
    if (!brk(addr)) {
        return reinterpret_cast<long>(get_program_break());
    } else {
        return reinterpret_cast<long>(old_break);
    }
}

#ifdef SYS_open
TRACEPOINT(trace_syscall_open, "%d <= \"%s\" 0x%x", int, const char *, int);
#endif
TRACEPOINT(trace_syscall_read, "0x%x <= %d %p 0x%x", ssize_t, int, char *, size_t);
TRACEPOINT(trace_syscall_uname, "%d <= ", int, struct utsname *);
TRACEPOINT(trace_syscall_write, "0x%x <= %d %p 0x%x", ssize_t, int, const void *, size_t);
TRACEPOINT(trace_syscall_gettid, "%d <=", pid_t);
TRACEPOINT(trace_syscall_clock_gettime, "%d <= %d %p", int, clockid_t, struct timespec *);
TRACEPOINT(trace_syscall_clock_getres, "%d <= %d %p", int, clockid_t, struct timespec *);
TRACEPOINT(trace_syscall_futex, "%d <= %p %d %d %p %p %d", int, int *, int, int, const struct timespec *, int *, uint32_t);
TRACEPOINT(trace_syscall_close, "%d <= %d", int, int);
TRACEPOINT(trace_syscall_pipe2, "%d <= %p 0%0o", int, int *, int);
TRACEPOINT(trace_syscall_epoll_create1, "%d <= 0%0o", int, int);
TRACEPOINT(trace_syscall_eventfd2, "%d <= %u 0%0o", int, unsigned int, int);
TRACEPOINT(trace_syscall_epoll_ctl, "%d <= %d %d %d 0x%x", int, int, int, int, struct epoll_event *);
#ifdef SYS_epoll_wait
TRACEPOINT(trace_syscall_epoll_wait, "%d <= %d 0x%x %d %d", int, int, struct epoll_event *, int, int);
#endif
TRACEPOINT(trace_syscall_accept4, "%d <= %d 0x%x %p 0%0o", int, int, struct sockaddr *, socklen_t *, int);
TRACEPOINT(trace_syscall_connect, "%d <= %d 0x%x %d", int, int, struct sockaddr *, socklen_t);
TRACEPOINT(trace_syscall_get_mempolicy, "%lu <= %p %p %lu %p %d", long, int *, unsigned long *, unsigned long, void *, int);
TRACEPOINT(trace_syscall_sys_sched_getaffinity, "%d <= %d %u %p", int, pid_t, unsigned, unsigned long *);
TRACEPOINT(trace_syscall_long_mmap, "0x%x <= 0x%x %lu %d %d %d %lu", long, void *, size_t, int, int, int, off_t);
TRACEPOINT(trace_syscall_munmap, "%d <= 0x%x %lu", int, void *, size_t);
TRACEPOINT(trace_syscall_rt_sigaction, "%d <= %d %p %p %lu", int, int, const struct k_sigaction *, struct k_sigaction *, size_t);
TRACEPOINT(trace_syscall_rt_sigprocmask, "%d <= %d %p %p %lu", int, int, sigset_t *, sigset_t *, size_t);
TRACEPOINT(trace_syscall_sys_exit, "%d <= %d", int, int);
TRACEPOINT(trace_syscall_sigaltstack, "%d <= %p %p", int, const stack_t *, stack_t *);
#ifdef SYS_select
TRACEPOINT(trace_syscall_select, "%d <= %d %p %p %p %p", int, int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
TRACEPOINT(trace_syscall_madvise, "%d <= 0x%x %lu %d", int, void *, size_t, int);
TRACEPOINT(trace_syscall_sched_yield, "%d <=", int);
TRACEPOINT(trace_syscall_mincore, "%d <= 0x%x %lu %p", int, void *, size_t, unsigned char *);
TRACEPOINT(trace_syscall_openat, "%d <= %d \"%s\" 0%0o %d", int, int, const char *, int, mode_t);
TRACEPOINT(trace_syscall_socket, "%d <= %d %d %d", int, int, int, int);
TRACEPOINT(trace_syscall_setsockopt, "%d <= %d %d %d %p %d", int, int, int, int, char *, int);
TRACEPOINT(trace_syscall_getsockopt, "%d <= %d %d %d %p %p", int, int, int, int, char *, unsigned int *);
TRACEPOINT(trace_syscall_getpeername, "%d <= %d %p %p", int, int, struct sockaddr *, unsigned int *);
TRACEPOINT(trace_syscall_bind, "%d <= %d %p %d", int, int, struct sockaddr *, int);
TRACEPOINT(trace_syscall_listen, "%d <= %d %d", int, int, int);
TRACEPOINT(trace_syscall_sys_ioctl, "%d <= %u %u %lu", int, unsigned int, unsigned int, unsigned long);
#ifdef SYS_stat
TRACEPOINT(trace_syscall_stat, "%d <= \"%s\" %p", int, const char *, struct stat *);
#endif
TRACEPOINT(trace_syscall_fstat, "%d <= %d %p", int, int, struct stat *);
TRACEPOINT(trace_syscall_getsockname, "%d <= %d %p %p", int, int, struct sockaddr *, socklen_t *);
TRACEPOINT(trace_syscall_sendto, "%lu <= %d 0x%x %lu %d %p %u", ssize_t, int, const void *, size_t, int, const struct sockaddr *, socklen_t);
TRACEPOINT(trace_syscall_sendmsg, "%lu <= %d %p %d", ssize_t, int, const struct msghdr *, int);
TRACEPOINT(trace_syscall_recvfrom, "%lu <= %d 0x%x %lu %d %p %p", ssize_t, int, void *, size_t, int, struct sockaddr *, socklen_t *);
TRACEPOINT(trace_syscall_recvmsg, "%lu <= %d %p %d", ssize_t, int, struct msghdr *, int);
TRACEPOINT(trace_syscall_dup3, "%d <= %d %d %d", int, int, int, int);
TRACEPOINT(trace_syscall_flock, "%d <= %d %d", int, int, int);
TRACEPOINT(trace_syscall_pwrite64, "%lu <= %d 0x%x %lu %ld", ssize_t, int, const void *, size_t, off_t);
TRACEPOINT(trace_syscall_fdatasync, "%d <= %d", int, int);
TRACEPOINT(trace_syscall_pselect6, "%d <= %d %p %p %p %p %p", int, int, fd_set *, fd_set *, fd_set *, struct timespec *, sys_sigset*);
TRACEPOINT(trace_syscall_fcntl, "%d <= %d %d %d", int, int, int, int);
TRACEPOINT(trace_syscall_pread64, "%lu <= %d 0x%x %lu %ld", ssize_t, int, void *, size_t, off_t);
TRACEPOINT(trace_syscall_ftruncate, "%d <= %d %ld", int, int, off_t);
TRACEPOINT(trace_syscall_fsync, "%d <= %d", int, int);
TRACEPOINT(trace_syscall_epoll_pwait, "%d <= %d %p %d %d %p", int, int, struct epoll_event *, int, int, const sigset_t*);
TRACEPOINT(trace_syscall_getrandom, "%lu <= 0x%x %lu %u", ssize_t, char *, size_t, unsigned int);
TRACEPOINT(trace_syscall_nanosleep, "%d <= %p %p", int, const struct timespec*, struct timespec *);
TRACEPOINT(trace_syscall_fstatat, "%d <= %d \"%s\" %p 0%0o", int, int, const char *, struct stat *, int);
TRACEPOINT(trace_syscall_sys_exit_group, "%d <= %d", int, int);
TRACEPOINT(trace_syscall_sys_getcwd, "%ld <= 0%0o %lu", long, char *, unsigned long);
TRACEPOINT(trace_syscall_readlinkat, "%lu <= %d 0%0o 0x%x %lu", ssize_t, int, const char *, char *, size_t);
TRACEPOINT(trace_syscall_getpid, "%d <=", pid_t);
TRACEPOINT(trace_syscall_set_mempolicy, "%ld <= %d %p %lu", long, int, unsigned long *, unsigned long);
TRACEPOINT(trace_syscall_sys_sched_setaffinity, "%d <= %d %u %p", int, pid_t, unsigned, unsigned long *);
#ifdef SYS_mkdir
TRACEPOINT(trace_syscall_mkdir, "%d <= \"%s\" %d", int, const char*, mode_t);
#endif
TRACEPOINT(trace_syscall_mkdirat, "%d <= %d \"%s\" %d", int, int, const char*, mode_t);
TRACEPOINT(trace_syscall_tgkill, "%d <= %d %d %d", int, int, int, int);
TRACEPOINT(trace_syscall_getgid, "%d <=", gid_t);
TRACEPOINT(trace_syscall_getuid, "%d <=", uid_t);
TRACEPOINT(trace_syscall_lseek, "%ld <= %d %ld %d", off_t, int, off_t, int);
TRACEPOINT(trace_syscall_statfs, "%d <= \"%s\" %p", int, const char *, struct statfs *);
TRACEPOINT(trace_syscall_unlinkat, "%d <= %d \"%s\" %d", int, int, const char *, int);
TRACEPOINT(trace_syscall_symlinkat, "%d <= \"%s\" %d \"%s\"", int, const char *, int, const char *);
TRACEPOINT(trace_syscall_sys_getdents64, "%lu <= %d 0x%x %lu", ssize_t, int, void *, size_t);
TRACEPOINT(trace_syscall_renameat, "%d <= %d \"%s\" %d \"%s\"", int, int, const char *, int, const char *);
TRACEPOINT(trace_syscall_sys_brk,"0x%x <= 0x%x",  long, void *);
TRACEPOINT(trace_syscall_clock_nanosleep, "%d <= %d %d %p %p", int, clockid_t, int, const struct timespec *, struct timespec *);
TRACEPOINT(trace_syscall_mknodat, "%d <= %d \"%s\" %d %d", int, int, const char *, mode_t, dev_t);
TRACEPOINT(trace_syscall_statx, "%d <= %d \"%s\" %d %u %p", int, int, const char *, int, unsigned int, struct statx *);
TRACEPOINT(trace_syscall_sys_getcpu, "%ld <= %p %p 0x%x", long, unsigned int *, unsigned int *, void *);
TRACEPOINT(trace_syscall_dup, "%d <= %d", int, int);
TRACEPOINT(trace_syscall_dup2, "%d <= %d %d", int, int, int);
TRACEPOINT(trace_syscall_mprotect, "%d <= 0x%x %lu %d", int, void *, size_t, int);
TRACEPOINT(trace_syscall_access, "%d <= \"%s\" %d", int, const char *, int);
TRACEPOINT(trace_syscall_writev, "%lu <= %d %p %d", ssize_t, int, const struct iovec *, int);
TRACEPOINT(trace_syscall_readlink, "%lu <= \"%s\" 0x%x %lu", ssize_t, const char *, char *, size_t);
TRACEPOINT(trace_syscall_geteuid, "%d <=", uid_t);
TRACEPOINT(trace_syscall_getegid, "%d <=", gid_t);
TRACEPOINT(trace_syscall_gettimeofday, "%d <= %p %p", int, struct timeval *, struct timezone *);
TRACEPOINT(trace_syscall_poll, "%d <= %p %ld %d", int, struct pollfd *, nfds_t, int);
TRACEPOINT(trace_syscall_getppid, "%d <=", pid_t);
TRACEPOINT(trace_syscall_epoll_create, "%d <= %d", int, int);
TRACEPOINT(trace_syscall_sysinfo, "%d <= %p", int, struct sysinfo *);
TRACEPOINT(trace_syscall_time, "%ld <= %p", time_t, time_t *);
TRACEPOINT(trace_syscall_sendfile, "%lu <= %d %d %p %lu", ssize_t, int, int, off_t *, size_t);
TRACEPOINT(trace_syscall_socketpair, "%d <= %d %d %d %p", int, int, int, int, int *);
TRACEPOINT(trace_syscall_shutdown, "%d <= %d %d", int, int, int);
TRACEPOINT(trace_syscall_unlink, "%d <= \"%s\"", int, const char *);
TRACEPOINT(trace_syscall_readv, "%lu <= %lu %p %lu", ssize_t, unsigned long, const struct iovec *, unsigned long);
TRACEPOINT(trace_syscall_getrusage, "%d <= %d %p", int, int, struct rusage *);
TRACEPOINT(trace_syscall_accept, "%d <= %d %p %p", int, int, struct sockaddr *, socklen_t *);
TRACEPOINT(trace_syscall_fchdir, "%d <= %u", int, unsigned int);
TRACEPOINT(trace_syscall_pipe, "%d <= %p", int, int*);
TRACEPOINT(trace_syscall_fstatfs, "%d <= %u %p", int, unsigned int, struct statfs *);
TRACEPOINT(trace_syscall_umask, "%d <= %d", mode_t, mode_t);
TRACEPOINT(trace_syscall_prctl, "%d <= %d %lu %lu %lu %lu", int, int, unsigned long, unsigned long, unsigned long, unsigned long);
TRACEPOINT(trace_syscall_chdir, "%d <= \"%s\"", int, const char *);
TRACEPOINT(trace_syscall_faccessat, "%d <= %d \"%s\" %d %d", int, int, const char *, int, int);
TRACEPOINT(trace_syscall_kill, "%d <= %d %d", int, pid_t, int);
TRACEPOINT(trace_syscall_alarm, "%d <= %u", int, unsigned int);
TRACEPOINT(trace_syscall_utimensat, "%d <= %d \"%s\" %p %d", int, int, const char *, const struct timespec*, int);
TRACEPOINT(trace_syscall_symlink, "%d <= \"%s\" \"%s\"", int, const char *, const char *);
TRACEPOINT(trace_syscall_rmdir, "%d <= \"%s\"", int, const char *);
TRACEPOINT(trace_syscall_sethostname, "%d <= \"%s\" %d", int, const char *, int);
TRACEPOINT(trace_syscall_creat, "%d <= \"%s\" %d", int, const char *, mode_t);
TRACEPOINT(trace_syscall_timerfd_create, "%d <= %d %d", int, int, int);
TRACEPOINT(trace_syscall_timerfd_settime, "%d <= %d %d %p %p", int, int, int, const struct itimerspec *, struct itimerspec *);
TRACEPOINT(trace_syscall_timerfd_gettime, "%d <= %d %p", int, int, struct itimerspec*);
TRACEPOINT(trace_syscall_chmod, "%d <= \"%s\" %d", int, const char *, mode_t);
TRACEPOINT(trace_syscall_fchmod, "%d <= %d %d", int, int, mode_t);

OSV_LIBC_API long syscall(long number, ...)
{
    // Save FPU state and restore it at the end of this function
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);

    switch (number) {
#ifdef SYS_open
    SYSCALL2(open, const char *, int);
#endif
    SYSCALL3(read, int, char *, size_t);
    SYSCALL1(uname, struct utsname *);
    SYSCALL3(write, int, const void *, size_t);
    SYSCALL0(gettid);
    SYSCALL2(clock_gettime, clockid_t, struct timespec *);
    SYSCALL2(clock_getres, clockid_t, struct timespec *);
    SYSCALL6(futex, int *, int, int, const struct timespec *, int *, uint32_t);
    SYSCALL1(close, int);
    SYSCALL2(pipe2, int *, int);
    SYSCALL1(epoll_create1, int);
    SYSCALL2(eventfd2, unsigned int, int);
    SYSCALL4(epoll_ctl, int, int, int, struct epoll_event *);
#ifdef SYS_epoll_wait
    SYSCALL4(epoll_wait, int, struct epoll_event *, int, int);
#endif
    SYSCALL4(accept4, int, struct sockaddr *, socklen_t *, int);
    SYSCALL3(connect, int, struct sockaddr *, socklen_t);
    SYSCALL5(get_mempolicy, int *, unsigned long *, unsigned long, void *, int);
    SYSCALL3(sys_sched_getaffinity, pid_t, unsigned, unsigned long *);
    SYSCALL6(long_mmap, void *, size_t, int, int, int, off_t);
    SYSCALL2(munmap, void *, size_t);
    SYSCALL4(rt_sigaction, int, const struct k_sigaction *, struct k_sigaction *, size_t);
    SYSCALL4(rt_sigprocmask, int, sigset_t *, sigset_t *, size_t);
    SYSCALL1(sys_exit, int);
    SYSCALL2(sigaltstack, const stack_t *, stack_t *);
#ifdef SYS_select
    SYSCALL5(select, int, fd_set *, fd_set *, fd_set *, struct timeval *);
#endif
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
#ifdef SYS_stat
    SYSCALL2(stat, const char *, struct stat *);
#endif
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
    SYSCALL6(pselect6, int, fd_set *, fd_set *, fd_set *, struct timespec *, sys_sigset*);
    SYSCALL3(fcntl, int, int, int);
    SYSCALL4(pread64, int, void *, size_t, off_t);
    SYSCALL2(ftruncate, int, off_t);
    SYSCALL1(fsync, int);
    SYSCALL5(epoll_pwait, int, struct epoll_event *, int, int, const sigset_t*);
    SYSCALL3(getrandom, char *, size_t, unsigned int);
    SYSCALL2(nanosleep, const struct timespec*, struct timespec *);
    SYSCALL4(fstatat, int, const char *, struct stat *, int);
    SYSCALL1(sys_exit_group, int);
    SYSCALL2(sys_getcwd, char *, unsigned long);
    SYSCALL4(readlinkat, int, const char *, char *, size_t);
    SYSCALL0(getpid);
    SYSCALL3(set_mempolicy, int, unsigned long *, unsigned long);
    SYSCALL3(sys_sched_setaffinity, pid_t, unsigned, unsigned long *);
#ifdef SYS_mkdir
    SYSCALL2(mkdir, const char*, mode_t);
#endif
    SYSCALL3(mkdirat, int, const char*, mode_t);
    SYSCALL3(tgkill, int, int, int);
    SYSCALL0(getgid);
    SYSCALL0(getuid);
    SYSCALL3(lseek, int, off_t, int);
    SYSCALL2(statfs, const char *, struct statfs *);
    SYSCALL3(unlinkat, int, const char *, int);
    SYSCALL3(symlinkat, const char *, int, const char *);
    SYSCALL3(sys_getdents64, int, void *, size_t);
    SYSCALL4(renameat, int, const char *, int, const char *);
    SYSCALL1(sys_brk, void *);
    SYSCALL4(clock_nanosleep, clockid_t, int, const struct timespec *, struct timespec *);
    SYSCALL4(mknodat, int, const char *, mode_t, dev_t);
    SYSCALL5(statx, int, const char *, int, unsigned int, struct statx *);
    SYSCALL3(sys_getcpu, unsigned int *, unsigned int *, void *);
    SYSCALL1(dup, int);
    SYSCALL2(dup2, int, int);
    SYSCALL3(mprotect, void *, size_t, int);
    SYSCALL2(access, const char *, int);
    SYSCALL3(writev, int, const struct iovec *, int);
    SYSCALL3(readlink, const char *, char *, size_t);
    SYSCALL0(geteuid);
    SYSCALL0(getegid);
    SYSCALL2(gettimeofday, struct timeval *, struct timezone *);
    SYSCALL3(poll, struct pollfd *, nfds_t, int);
    SYSCALL0(getppid);
    SYSCALL1(epoll_create, int);
    SYSCALL1(sysinfo, struct sysinfo *);
    SYSCALL1(time, time_t *);
    SYSCALL4(sendfile, int, int, off_t *, size_t);
    SYSCALL4(socketpair, int, int, int, int *);
    SYSCALL2(shutdown, int, int);
    SYSCALL1(unlink, const char *);
    SYSCALL3(readv, unsigned long, const struct iovec *, unsigned long);
    SYSCALL2(getrusage, int, struct rusage *);
    SYSCALL3(accept, int, struct sockaddr *, socklen_t *);
    SYSCALL1(fchdir, unsigned int);
    SYSCALL1(pipe, int*);
    SYSCALL2(fstatfs, unsigned int, struct statfs *);
    SYSCALL1(umask, mode_t);
    SYSCALL5(prctl, int, unsigned long, unsigned long, unsigned long, unsigned long);
    SYSCALL1(chdir, const char *);
    SYSCALL4(faccessat, int, const char *, int, int);
    SYSCALL2(kill, pid_t, int);
    SYSCALL1(alarm, unsigned int);
    SYSCALL4(utimensat, int, const char *, const struct timespec*, int);
    SYSCALL2(symlink, const char *, const char *);
    SYSCALL1(rmdir, const char *);
    SYSCALL2(sethostname, const char *, int);
    SYSCALL2(creat, const char *, mode_t);
    SYSCALL2(timerfd_create, int, int);
    SYSCALL4(timerfd_settime, int, int, const struct itimerspec *, struct itimerspec *);
    SYSCALL2(timerfd_gettime, int, struct itimerspec*);
    SYSCALL2(chmod, const char *, mode_t);
    SYSCALL2(fchmod, int, mode_t);
    }

    debug_always("syscall(): unimplemented system call %d\n", number);
    errno = ENOSYS;
    return -1;
}
long __syscall(long number, ...)  __attribute__((alias("syscall")));

#ifdef __x86_64__
// In x86-64, a SYSCALL instruction has exactly 6 parameters, because this is the number of registers
// alloted for passing them (additional parameters *cannot* be passed on the stack). So we can get
// 7 arguments to this function (syscall number plus its 6 parameters). Because in the x86-64 ABI the
// seventh argument is on the stack, we must pass the arguments explicitly to the syscall() function
// and can't just call it without any arguments and hope everything will be passed on
extern "C" long syscall_wrapper(long number, long p1, long p2, long p3, long p4, long p5, long p6)
#endif
#ifdef __aarch64__
// In aarch64, the first 8 parameters to a procedure call are passed in the x0-x7 registers and
// the parameters of syscall call (SVC intruction) in are passed in x0-x5 registers and syscall number
// in x8 register before. To avoid shuffling the arguments around we make syscall_wrapper()
// accept the syscall parameters as is but accept the syscall number as the last 7th argument which
// the code in entry.S arranges.
extern "C" long syscall_wrapper(long p1, long p2, long p3, long p4, long p5, long p6, long number)
#endif
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
