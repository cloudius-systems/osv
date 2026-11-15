/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "signal.hh"
#include <string.h>
#include <unistd.h>
#include <string.h>
#include <osv/debug.hh>
#include <osv/printf.hh>
#include <osv/sched.hh>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/power.hh>
#include <osv/clock.hh>
#include <api/setjmp.h>
#include <osv/stubbing.hh>
#include <osv/pid.h>
#include <osv/export.h>
#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <osv/fork_arena.hh>
#include <osv/mmu.hh>
#include <osv/fork.hh>
#endif
#include <unordered_map>
#include <boost/intrusive/set.hpp>

using namespace osv::clock::literals;
namespace bi = boost::intrusive;

namespace osv {

// we can't use have __thread sigset because of the constructor
__thread __attribute__((aligned(sizeof(sigset))))
    char thread_signal_mask[sizeof(sigset)];

// Let's ignore rt signals. For standard signals, signal(7) says order is
// unspecified over multiple deliveries, so we always record the last one.  It
// also relieves us of any need for locking, since it doesn't matter if the
// pending signal changes: returning any one is fine
__thread int thread_pending_signal;

// Bitmask (indexed by signal-1) of signals the current thread is ACTIVELY
// blocked on inside sigwait()/sigtimedwait() right now.  This distinguishes a
// thread that is CONSUMING a signal via sigwait (delivery goes to it, not to a
// handler) from a thread that merely BLOCKED the signal with sigprocmask (the
// signal's handler must still run, e.g. PostgreSQL's SIGCHLD reaper).  A thread
// can only be in one sigwait at a time, so a plain per-thread mask suffices.
__thread unsigned long thread_sigwaiting[(nsignals + 63) / 64];

static inline void set_sigwaiting(unsigned sigidx, bool on)
{
    unsigned long bit = 1UL << (sigidx & 63);
    if (on) {
        thread_sigwaiting[sigidx >> 6] |= bit;
    } else {
        thread_sigwaiting[sigidx >> 6] &= ~bit;
    }
}

struct sigaction signal_actions[nsignals];

#if CONF_fork
// Signal dispositions are per-PROCESS in POSIX, but OSv keeps a single global
// signal_actions[] and collapses every fork "process" onto one pid (OSV_PID).
// A fork child that calls sigaction() (e.g. PostgreSQL's startup process
// resetting its inherited handlers) would therefore CLOBBER the parent
// postmaster's handlers in the shared table -- most damagingly the SIGCHLD
// reaper the postmaster relies on to reap children and advance to PM_RUN.
//
// To model per-process dispositions we give each fork-child address space its
// own copy of signal_actions[], initialized from the parent's at fork time and
// only mutated by that child's own sigaction() calls.  The top-level
// application (AS0 / kernel address space) keeps using the global array, so the
// heavily-tested non-fork path is byte-identical.
//
// Synchronous signals to the current thread (generate_signal: SIGSEGV/SIGFPE)
// and sigaction() use the CURRENT context's table.  Asynchronous kill() to the
// process (getpid()) uses the TOP-LEVEL (global) table: the fork-synthesized
// SIGCHLD that a child raises to the postmaster on exit must run the
// postmaster's handler, not the exiting child's (which has been reset).
extern "C" struct sigaction *fork_child_signal_actions();  // this child's table or nullptr
extern "C" struct sigaction *fork_child_signal_actions_for(mmu::address_space *child_as);  // a specific child's table
extern "C" void fork_init_child_signal_actions(mmu::address_space *child_as,
                                    const struct sigaction *parent_table);
extern "C" void fork_release_child_signal_actions(mmu::address_space *child_as);
// Table for sigaction()/generate_signal(): the current fork child's private
// copy if we are in one, else the global array.
static inline struct sigaction *current_signal_actions()
{
    if (auto *t = fork_child_signal_actions()) {
        return t;
    }
    return signal_actions;
}
#else
static inline struct sigaction *current_signal_actions()
{
    return signal_actions;
}
#endif

sigset* from_libc(sigset_t* s)
{
    return reinterpret_cast<sigset*>(s);
}

const sigset* from_libc(const sigset_t* s)
{
    return reinterpret_cast<const sigset*>(s);
}

sigset* thread_signals()
{
    return reinterpret_cast<sigset*>(thread_signal_mask);
}

sigset* thread_signals(sched::thread *t)
{
    return t->remote_thread_local_ptr<sigset>(&thread_signal_mask);
}

inline bool is_sig_dfl(const struct sigaction &sa) {
    if (sa.sa_flags & SA_SIGINFO) {
         return sa.sa_sigaction == nullptr; // a non-standard Linux extension
    } else {
         return sa.sa_handler == SIG_DFL;
    }
}

inline bool is_sig_ign(const struct sigaction &sa) {
    return (!(sa.sa_flags & SA_SIGINFO) && sa.sa_handler == SIG_IGN);
}

//Similar to signal actions and mask, list of "waiters" for a given signal
//with number "signo" is stored at the index "signo - 1"
typedef std::list<sched::thread *> thread_list;
static std::array<thread_list, nsignals> waiters;
mutex waiters_mutex;

int wake_up_signal_waiters(int signo)
{
    SCOPE_LOCK(waiters_mutex);
    int consumers = 0;

    unsigned sigidx = signo - 1;
    for (auto& t: waiters[sigidx]) {
        // Wake every thread blocked on this signal (unchanged), so a thread in
        // sigwait()/sigtimedwait() sees thread_pending_signal and returns.
        t->remote_thread_local_var<int>(thread_pending_signal) = signo;
        t->wake();
        // Only a thread ACTIVELY inside sigwait()/sigtimedwait() for this
        // signal is CONSUMING it; a thread that merely sigprocmask()-blocked it
        // is not.  Return a nonzero "consumed" count only for the former, so
        // kill() runs the handler when nobody is actually sigwaiting (the
        // PostgreSQL SIGCHLD case).
        unsigned long bit = 1UL << (sigidx & 63);
        if (t->remote_thread_local_ptr<unsigned long>(&thread_sigwaiting)[sigidx >> 6] & bit) {
            consumers++;
        }
    }
    return consumers;
}

void wait_for_signal(unsigned int sigidx)
{
    SCOPE_LOCK(waiters_mutex);
#if CONF_fork
    // `waiters` is a GLOBAL kernel structure manipulated cross-address-space:
    // a fork child and its parent both push/remove on the same list.  Its
    // std::list nodes must therefore live in the identity-mapped kernel heap,
    // never the copy-on-write fork arena -- a COW-private node would corrupt
    // the shared list's next/prev pointers as seen from the other address
    // space, and a later traversal (unwait_for_signal -> list::remove) would
    // follow a dangling link and fault.  This is
    // the same rule already applied to thread objects and wait_records.
    fork_arena::kernel_heap_scope kh;
#endif
    waiters[sigidx].push_front(sched::thread::current());
}

void unwait_for_signal(sched::thread *t, unsigned int sigidx)
{
    SCOPE_LOCK(waiters_mutex);
#if CONF_fork
    // Match wait_for_signal(): keep list-node churn on the identity heap so the
    // shared cross-AS list is never a COW arena page.
    fork_arena::kernel_heap_scope kh;
#endif
    waiters[sigidx].remove(t);
}

void unwait_for_signal(unsigned int sigidx)
{
    unwait_for_signal(sched::thread::current(), sigidx);
}

void __attribute__((constructor)) signals_register_thread_notifier()
{
    sched::thread::register_exit_notifier(
        []() {
            sigset *set = thread_signals();
            if (!set->mask.any()) { return; }
            for (unsigned i = 0; i < nsignals; ++i) {
                if (set->mask.test(i)) {
                    unwait_for_signal(i);
                }
            }
        }
    );
}

void generate_signal(siginfo_t &siginfo, exception_frame* ef)
{
    unsigned sigidx = siginfo.si_signo - 1;
    if (pthread_self() && thread_signals()->mask[sigidx]) {
        // FIXME: need to find some other thread to deliver
        // FIXME: the signal to.
        //
        // There are certainly no waiters for this, because since we
        // only deliver signals through this method directly, the thread
        // needs to be running to generate them. So definitely not waiting.
        abort();
    }
    if (is_sig_dfl(current_signal_actions()[sigidx])) {
        // Our default is to abort the process
        abort();
    } else if(!is_sig_ign(current_signal_actions()[sigidx])) {
        arch::build_signal_frame(ef, siginfo, current_signal_actions()[sigidx]);
    }
}

void handle_mmap_fault(ulong addr, int sig, exception_frame* ef)
{
    siginfo_t si;
    si.si_signo = sig;
    si.si_addr = reinterpret_cast<void*>(addr);
    generate_signal(si, ef);
}

}

using namespace osv;

OSV_LIBC_API
int sigemptyset(sigset_t* sigset)
{
    from_libc(sigset)->mask.reset();
    return 0;
}

OSV_LIBC_API
int sigfillset(sigset_t *sigset)
{
    from_libc(sigset)->mask.set();
    return 0;
}

OSV_LIBC_API
int sigaddset(sigset_t *sigset, int signum)
{
    if (signum < 1 || signum > (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    unsigned sigidx = signum - 1;
    from_libc(sigset)->mask.set(sigidx);
    return 0;
}

OSV_LIBC_API
int sigdelset(sigset_t *sigset, int signum)
{
    if (signum < 1 || signum > (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    unsigned sigidx = signum - 1;
    from_libc(sigset)->mask.reset(sigidx);
    return 0;
}

OSV_LIBC_API
int sigismember(const sigset_t *sigset, int signum)
{
    if (signum < 1 || signum > (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    unsigned sigidx = signum - 1;
    return from_libc(sigset)->mask.test(sigidx);
}

OSV_LIBC_API
int sigprocmask(int how, const sigset_t* _set, sigset_t* _oldset)
{
    auto set = from_libc(_set);
    auto oldset = from_libc(_oldset);
    if (oldset) {
        *oldset = *thread_signals();
    }
    if (set) {
        switch (how) {
        case SIG_BLOCK:
            for (unsigned i = 0; i < nsignals; ++i) {
                if (set->mask.test(i)) {
                    wait_for_signal(i);
                }
            }
            thread_signals()->mask |= set->mask;
            break;
        case SIG_UNBLOCK:
            for (unsigned i = 0; i < nsignals; ++i) {
                if (set->mask.test(i)) {
                    unwait_for_signal(i);
                }
            }
            thread_signals()->mask &= ~set->mask;
            break;
        case SIG_SETMASK:
            for (unsigned i = 0; i < nsignals; ++i) {
                unwait_for_signal(i);
                if (set->mask.test(i)) {
                    wait_for_signal(i);
                }
            }
            thread_signals()->mask = set->mask;
            break;
        default:
            abort();
        }
    }
    return 0;
}

UNIMPL(OSV_LIBC_API int sigsuspend(const sigset_t *mask));

OSV_LIBC_API
int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    // FIXME: We do not support any sa_flags besides SA_SIGINFO.
    if (signum < 1 || signum > (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    unsigned sigidx = signum - 1;
    struct sigaction *table = current_signal_actions();
    if (oldact) {
        *oldact = table[sigidx];
    }
    if (act) {
        table[sigidx] = *act;
    }
    return 0;
}

// using signal() is not recommended (use sigaction instead!), but some
// programs like to call to do simple things, like ignoring a certain signal.
static sighandler_t signal(int signum, sighandler_t handler, int sa_flags)
{
    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = handler;
    act.sa_flags = sa_flags;
    struct sigaction old;
    if (sigaction(signum, &act, &old) < 0) {
        return SIG_ERR;
    }
    if (old.sa_flags & SA_SIGINFO) {
        // TODO: Is there anything sane to do here?
        return nullptr;
    } else {
        return old.sa_handler;
    }
}

OSV_LIBC_API
sighandler_t signal(int signum, sighandler_t handler)
{
    return signal(signum, handler, SA_RESTART);
}

extern "C"
OSV_LIBC_API
sighandler_t __sysv_signal(int signum, sighandler_t handler)
{
    return signal(signum, handler, SA_RESETHAND | SA_NODEFER);
}

// using sigignore() and friends is not recommended as it is obsolete System V
// APIs. Nevertheless, some programs use it.
OSV_LIBC_API
int sigignore(int signum)
{
    if (signum < 1 || signum > (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    struct sigaction act;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_IGN;
    return sigaction(signum, &act, nullptr);
}

OSV_LIBC_API
int sigwait(const sigset_t *set, int *sig)
{
    // Mark the signals in @set as actively being CONSUMED via sigwait, so a
    // concurrent kill() delivers to us (and skips the handler) rather than
    // running the installed handler.  Cleared on return.
    if (set) {
        for (unsigned i = 0; i < nsignals; ++i) {
            if (sigismember(set, i + 1)) {
                set_sigwaiting(i, true);
            }
        }
    }
    sched::thread::wait_until([sig] { return *sig = thread_pending_signal; });
    thread_pending_signal = 0;
    if (set) {
        for (unsigned i = 0; i < nsignals; ++i) {
            if (sigismember(set, i + 1)) {
                set_sigwaiting(i, false);
            }
        }
    }
    return 0;
}

// Timed variant used by rt_sigtimedwait(2).  Waits for one of the signals in
// @set to be delivered, or for the relative @timeout to elapse.  A {0,0}
// timeout polls (returns immediately with EAGAIN if nothing matching is
// pending).  Returns the signal number, or -1 with errno set (EAGAIN on
// timeout, EINVAL on a malformed timeout).  Called from linux.cc; kept here
// because it needs the signal-waiter internals.
extern "C" OSV_LIBC_API
int osv_sigtimedwait(const sigset_t *set, siginfo_t *si,
                     const struct timespec *timeout)
{
    // Validate the timeout as Linux does: a malformed timespec must fail with
    // EINVAL rather than arm a bogus (or negative) timer.
    if (!timeout || timeout->tv_sec < 0 ||
        timeout->tv_nsec < 0 || timeout->tv_nsec >= 1000000000) {
        errno = EINVAL;
        return -1;
    }
    sched::timer tmr(*sched::thread::current());
    tmr.set(osv::clock::uptime::now() +
            std::chrono::seconds(timeout->tv_sec) +
            std::chrono::nanoseconds(timeout->tv_nsec));

    // Mark the @set signals as actively consumed via sigtimedwait (see sigwait
    // and wake_up_signal_waiters): while we are here a concurrent kill()
    // delivers to us rather than running the handler.
    if (set) {
        for (unsigned i = 0; i < nsignals; ++i) {
            if (sigismember(set, i + 1)) {
                set_sigwaiting(i, true);
            }
        }
    }

    // Only accept a delivered signal that is a member of @set (rt_sigtimedwait
    // waits for a specific set); a signal outside the set is not what the
    // caller asked for, so keep waiting for it (or the timeout).
    int signo = 0;
    sched::thread::wait_until([&] {
        int pending = thread_pending_signal;
        if (pending != 0 && (!set || sigismember(set, pending))) {
            signo = pending;
            return true;
        }
        return tmr.expired();
    });

    if (set) {
        for (unsigned i = 0; i < nsignals; ++i) {
            if (sigismember(set, i + 1)) {
                set_sigwaiting(i, false);
            }
        }
    }

    if (signo == 0) {
        // Timed out (or polled with {0,0}) with no matching signal.
        errno = EAGAIN;
        return -1;
    }
    thread_pending_signal = 0;
    if (si) {
        memset(si, 0, sizeof(*si));
        si->si_signo = signo;
    }
    return signo;
}

// Partially-Linux-compatible support for kill(2).
// Note that this is different from our generate_signal() - the latter is only
// suitable for delivering SIGFPE and SIGSEGV to the same thread that called
// this function.
//
// Handling kill(2)/signal(2) exactly like Linux, where one of the existing
// threads runs the signal handler, is difficult in OSv because it requires
// tracking of when we're in kernel code (to delay the signal handling until
// it returns to "user" code), and also to interrupt sleeping kernel code and
// have it return sooner.
// Instead, we provide a simple "approximation" of the signal handling -
// on each kill(), a *new* thread is created to run the signal handler code.
//
// This approximation will work in programs that do not care about the signal
// being delivered to a specific thread, and that do not intend that the
// signal should interrupt a system call (e.g., sleep() or hung read()).
// FIXME: think if our handling of nested signals is ok (right now while
// handling a signal, we can get another one of the same signal and start
// another handler thread. We should probably block this signal while
// handling it.

OSV_LIBC_API
int kill(pid_t pid, int sig)
{
#if CONF_fork
    // Under fork() each backend has a distinct pid (getpid()) and its own
    // address space + signal table.  A cross-backend kill (PostgreSQL's
    // WakeupOtherProc -> kill(other_backend_pid, SIGURG) to wake its latch)
    // must run the TARGET backend's handler in the TARGET's address space --
    // otherwise latch_sigurg_handler pokes the wrong self-pipe and the waiting
    // backend never wakes (a lock wait wedges: 0 tps, no failure).  Detect a
    // live-fork-child target and route accordingly.
    //
    // OSV_PID names the top-level app / kernel (AS0) -- e.g. the PostgreSQL
    // postmaster, whose getpid() is OSV_PID.  A forked backend signalling the
    // postmaster (a parallel-query leader's RegisterDynamicBackgroundWorker ->
    // kill(PostmasterPid==OSV_PID, SIGUSR1) to make the postmaster fork the
    // parallel workers; or pg_reload_conf() -> kill(PostmasterPid, SIGHUP))
    // must run the postmaster's handler in AS0, NOT be rejected.  AS0 is never
    // registered in g_pid_as, so as_for_pid(OSV_PID) returns nullptr; treat
    // OSV_PID like the self/broadcast case (target_as stays nullptr -> AS0)
    // instead of ESRCH -- otherwise the postmaster never launches the workers
    // and the parallel Gather hangs (leader waits forever for workers that
    // were never forked).
    mmu::address_space *target_as = nullptr;
    // A LIVE fork-backend's OWN latch/barrier self-signal (SIGURG/SIGUSR1)
    // must run its handler in the CALLER's own COW address space.  The base
    // kill() spawns a NEW detached thread and set_address_space()es it into
    // that COW AS -- but that fresh handler thread, running PG barrier code
    // (HandleProcSignalBarrierInterrupt / latch_sigurg_handler) against a
    // COW-lazy mmap page (0x2000..), faults and the fault NESTS fatally
    // ("exception nested too deeply", ~1-in-3 DROP DATABASE runs).  For a
    // self-signal the caller is ALREADY that backend, already in the right
    // COW AS, on a real faultable stack -- exactly where a real Unix signal
    // handler would run.  So run those two short async-signal-safe handlers
    // INLINE in the caller and skip the thread spawn entirely (self_inline).
    bool self_inline = false;
    if (pid != getpid() && pid != 0 && pid != -1 && pid != OSV_PID) {
        target_as = osv::fork::as_for_pid(pid);
        if (!target_as) {
            errno = ESRCH;
            return -1;
        }
    } else if (pid == getpid()) {
        // A live fork child signalling its own pid: use the child's signal
        // table and run the handler in the child's address space, as for any
        // other process.  PostgreSQL's statement and lock timeout handlers
        // send SIGINT to MyProcPid, and pg_terminate_backend() of one's own
        // backend sends SIGTERM; the postmaster's handlers for those start a
        // shutdown.  as_for_pid() gates this to a live registered child: the
        // top-level app (AS0) and a child mid-teardown get nullptr and keep
        // the global table.  The child-exit SIGCHLD goes to the parent's pid
        // (fork.cc child_exited), not here.
        target_as = osv::fork::as_for_pid(pid);
        // Two PostgreSQL self-signals run their handler INLINE in the caller,
        // which is already in this backend's COW address space on a faultable
        // stack, so the handler sets the flag / pokes the self-pipe in the
        // copy the backend actually reads:
        //   * WakeupMyProc() -> kill(MyProcPid, SIGURG): latch_sigurg_handler
        //     writes selfpipe_writefd to wake the backend's own WaitEventSet.
        //   * EmitProcSignalBarrier() -> kill(MyProcPid, SIGUSR1):
        //     HandleProcSignalBarrierInterrupt sets ProcSignalBarrierPending so
        //     the backend absorbs its OWN barrier (DROP DATABASE / DROP
        //     TABLESPACE otherwise wait for their own PID forever).
        // Only when the signal is UNBLOCKED in the calling thread: if PG has
        // it masked (a critical section), POSIX defers delivery, so keep the
        // deferred (thread-spawn) behavior rather than re-enter PG at a point
        // it masked the signal to protect.
        if (target_as && target_as == mmu::current_address_space() &&
            (sig == SIGURG || sig == SIGUSR1) &&
            !osv::thread_signals()->mask.test(sig - 1)) {
            self_inline = true;
        }
    }
#else
    // OSv only implements one process, whose pid is getpid().
    // Sending a signal to pid 0 or -1 is also fine, as it will also send a
    // signal to the same single process.
    if (pid != getpid() && pid != 0 && pid != -1) {
        errno = ESRCH;
        return -1;
    }
#endif
    if (sig == 0) {
        // kill() with signal 0 doesn't cause an actual signal 0, just
        // testing the pid.
        return 0;
    }
    if (sig < 0 || sig > (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    unsigned sigidx = sig - 1;
#if CONF_fork
    // A targeted live fork child uses THAT child's signal table (it installed
    // its own handler); everything else uses the global/top-level table.
    struct sigaction *actions = signal_actions;
    if (target_as) {
        struct sigaction *tt = fork_child_signal_actions_for(target_as);
        if (tt) {
            actions = tt;
        }
    }
#else
    struct sigaction *actions = signal_actions;
#endif
    if (is_sig_dfl(actions[sigidx])) {
        // Per POSIX, the default disposition of SIGCHLD, SIGURG and SIGWINCH is
        // to IGNORE (not to terminate the process).  OSv treats an unhandled
        // SIG_DFL signal as fatal and powers the VM off, which is wrong for
        // these three: e.g. delivering SIGCHLD to a process with no handler
        // installed must be silently ignored, not fatal.  Honor the ignore
        // default for them instead of powering off.  (OSv's fork() emulation
        // relies on this: it raises SIGCHLD to the parent when a child exits.)
        if (sig == SIGCHLD || sig == SIGURG || sig == SIGWINCH) {
            return 0;
        }
        // Our default is to power off.
        debugf("Uncaught signal %d (\"%s\"). Powering off.\n",
                sig, strsignal(sig));
        osv::poweroff();
    } else if(!is_sig_ign(actions[sigidx])) {
        if ((pid == OSV_PID) || (pid == 0) || (pid == -1)) {
            // This semantically means signalling everybody. So we will signal
            // every thread that is waiting for this.
            //
            // The thread does not expect the signal handler to still be delivered,
            // so if we wake up some folks (usually just the one waiter), we should
            // not continue processing.
            //
            // wake_up_signal_waiters() returns nonzero only if a thread is
            // actively CONSUMING this signal via sigwait()/sigtimedwait() (see
            // that function): such a thread takes delivery itself, so the
            // handler must NOT also run.  A thread that merely BLOCKED the
            // signal with sigprocmask (e.g. PostgreSQL's postmaster around its
            // epoll ServerLoop) is not consuming it -- for that case
            // wake_up_signal_waiters() returns 0 and we fall through to run the
            // installed handler (which is what pokes the latch/self-pipe that
            // wakes the postmaster on SIGCHLD).
            if (wake_up_signal_waiters(sig)) {
                return 0;
            }
        }

        // User-defined signal handler. Run it in a new thread. This isn't
        // very Unix-like behavior, but if we assume that the program doesn't
        // care which of its threads handle the signal - why not just create
        // a completely new thread and run it there...
        // The newly created thread is tagged as an application one
        // to make sure that user provided signal handler code has access to all
        // the features like syscall stack which matters for Golang apps
        const auto sa = actions[sigidx];
#if CONF_fork
        // Self-signal SIGURG/SIGUSR1 to our own live backend: run the handler
        // INLINE in the caller instead of spawning a thread + retargeting its
        // AS.  The caller is already in this backend's COW AS on a faultable
        // stack, so the COW-lazy mmap fault the spawned handler thread took
        // (and nested "exception nested too deeply") is handled normally here.
        // PG's latch_sigurg_handler / HandleProcSignalBarrierInterrupt are
        // short async-signal-safe routines (poke a pipe / set a flag), safe
        // to run synchronously in the calling thread.
        if (self_inline) {
            if (sa.sa_flags & SA_RESETHAND) {
                actions[sigidx].sa_flags = 0;
                actions[sigidx].sa_handler = SIG_DFL;
            }
            if (sa.sa_flags & SA_SIGINFO) {
                sa.sa_sigaction(sig, nullptr, nullptr);
            } else {
                sa.sa_handler(sig);
            }
            return 0;
        }
#endif
        auto t = sched::thread::make([=] {
            if (sa.sa_flags & SA_RESETHAND) {
                actions[sigidx].sa_flags = 0;
                actions[sigidx].sa_handler = SIG_DFL;
            }
            if (sa.sa_flags & SA_SIGINFO) {
                // FIXME: proper second (siginfo) and third (context) arguments (See example in call_signal_handler)
                sa.sa_sigaction(sig, nullptr, nullptr);
            } else {
                sa.sa_handler(sig);
            }
        }, sched::thread::attr().detached().stack(65536).name("signal_handler"),
                false, true);
#if CONF_fork
        // Where does the handler thread run?
        //  * Targeted at a live fork child (target_as != nullptr): run it in
        //    THAT child's address space, so its handler (PostgreSQL's
        //    latch_sigurg_handler) touches the TARGET backend's globals/fds
        //    (its self-pipe) -- the cross-backend latch wakeup.
        //  * Otherwise (process-level notify, e.g. the fork-synthesized SIGCHLD
        //    a child raises to the postmaster on exit): run it in AS0.  A newly
        //    created thread inherits the CREATING thread's address space; under
        //    fork a child raises SIGCHLD from its own dying COW address space,
        //    and the postmaster's process-level handler must run against the
        //    top-level app's globals (its latch/self-pipe), not the child's.
        //    The top-level app and kernel already have _current_as == AS0, so
        //    this is a no-op for them and the non-fork path is unchanged.
        t->set_address_space(target_as ? target_as : mmu::kernel_address_space());
#endif
        t->start();
    }
    return 0;
}

OSV_LIBC_API
int pause(void) {
    try
    {
        sched::thread::wait_until_interruptible([] {return false;});
    }
    catch (int e)
    {
        assert(e == EINTR);
    }

    errno = EINTR;
    return -1;
}

// Interval timers: setitimer(), getitimer() and alarm().
//
// POSIX gives each process its own ITIMER_REAL and ITIMER_VIRTUAL.  Under
// CONF_fork a process is an address space, so the timers are kept per address
// space: an entry is created on the first setitimer() in that address space and
// released with it (fork child teardown and execve()).  A fork child starts
// with no timer, as POSIX requires.  Expiry sends the signal to the pid that
// armed the timer, so it runs that process's handler.  Without CONF_fork there
// is one process and one pair of timers.
//
// One worker thread serves every timer: it sleeps until the earliest deadline
// of an ordered set of armed entries.  That keeps the thread count constant
// however many processes come and go, and arming or cancelling a timer
// allocates nothing once the entry exists.
//
// OSv has no per-process CPU-time accounting for the virtual timer, so
// ITIMER_VIRTUAL counts wall-clock time, as it always has here; it is per
// process like ITIMER_REAL.
//
// alarm() and setitimer() also remember the thread that armed the timer, so
// that expiry can interrupt a system call that thread is sleeping in.

namespace {

struct itimer_entry {
    bi::set_member_hook<> armed_hook;     // linked in itimers::armed while due
    osv::clock::uptime::time_point due;
    std::chrono::nanoseconds interval {};
    pid_t pid = 0;                        // the process the signal goes to
    unsigned owner_tid = 0;               // thread to interrupt, 0 for none
    int signum = 0;
    bool operator<(const itimer_entry &o) const {
        return due < o.due || (due == o.due && this < &o);
    }
};

struct process_itimers {
    itimer_entry t[2];                    // ITIMER_REAL, ITIMER_VIRTUAL
};

#if CONF_fork
using process_key = mmu::address_space *;
#else
using process_key = void *;
#endif

class itimers {
public:
    itimers();
    int set(int which, const struct itimerval *new_value,
            struct itimerval *old_value);
    int get(int which, struct itimerval *curr_value);
    void cancel_this_thread();
#if CONF_fork
    void release(process_key key);
#endif
private:
    void work();
    void disarm(itimer_entry &e);
    static void deliver(pid_t pid, int signum, unsigned owner_tid);

    mutex _mutex;
    condvar _cond;
    sched::thread *_worker;
    bool _started = false;
    std::unordered_map<process_key, process_itimers *> _by_process;
    bi::multiset<itimer_entry,
                 bi::member_hook<itimer_entry, bi::set_member_hook<>,
                                 &itimer_entry::armed_hook>> _armed;
};

static __thread bool t_owns_itimer;

static process_key current_process()
{
#if CONF_fork
    return mmu::current_address_space();
#else
    return nullptr;
#endif
}

static int which_index(int which)
{
    switch (which) {
    case ITIMER_REAL:
        return 0;
    case ITIMER_VIRTUAL:
        return 1;
    default:
        return -1;
    }
}

itimers::itimers()
    : _worker(sched::thread::make([this] { work(); },
                                  sched::thread::attr().name("itimer")))
{
}

// alarm() wants to know which thread ran it, so when the alarm happens it
// can interrupt a system call sleeping on this thread. But there's a special
// case: if alarm() is called in a signal handler (which currently in OSv is
// a separate thread), this probably means the alarm was re-set after the
// previous alarm expired. In that case we obviously don't want to remember
// the signal handler thread (which will go away almost immediately). What
// we'll do in the is_signal_handler() case is to just keep remembering the
// old owner thread, hoping it is still relevant...
static bool is_signal_handler(){
    // must be the same name used in kill() above
    return sched::thread::current()->name() == "signal_handler";
}

static void fill_value(const itimer_entry &e, struct itimerval *v)
{
    fill_tv(e.interval, &v->it_interval);
    if (!e.armed_hook.is_linked()) {
        v->it_value.tv_sec = v->it_value.tv_usec = 0;
    } else {
        auto left = e.due - osv::clock::uptime::now();
        if (left <= decltype(left)::zero()) {
            left = std::chrono::microseconds(1);
        }
        fill_tv(left, &v->it_value);
    }
}

void itimers::disarm(itimer_entry &e)
{
    if (e.armed_hook.is_linked()) {
        _armed.erase(_armed.iterator_to(e));
    }
}

int itimers::set(int which, const struct itimerval *new_value,
                 struct itimerval *old_value)
{
    int idx = which_index(which);
    if (idx < 0 || !new_value) {
        return EINVAL;
    }
    bool arm = new_value->it_value.tv_sec || new_value->it_value.tv_usec;
    auto key = current_process();
    pid_t pid = getpid();
    bool in_handler = is_signal_handler();
    WITH_LOCK(_mutex) {
        auto it = _by_process.find(key);
        if (it == _by_process.end()) {
            if (!arm) {
                if (old_value) {
                    *old_value = {};
                }
                return 0;
            }
#if CONF_fork
            // Shared by the worker and every address space: identity heap.
            fork_arena::kernel_heap_scope kh;
#endif
            auto *p = new process_itimers;
            p->t[0].signum = SIGALRM;
            p->t[1].signum = SIGVTALRM;
            it = _by_process.emplace(key, p).first;
        }
        auto &e = it->second->t[idx];
        if (old_value) {
            fill_value(e, old_value);
        }
        disarm(e);
        e.interval = std::chrono::nanoseconds::zero();
        if (!in_handler) {
            e.owner_tid = 0;
        }
        if (arm) {
            e.interval = new_value->it_interval.tv_sec * 1_s +
                         new_value->it_interval.tv_usec * 1_us;
            e.due = osv::clock::uptime::now() +
                    new_value->it_value.tv_sec * 1_s +
                    new_value->it_value.tv_usec * 1_us;
            e.pid = pid;
            if (!in_handler) {
                e.owner_tid = sched::thread::current()->id();
                t_owns_itimer = true;
            }
            _armed.insert(e);
            if (!_started) {
                _worker->start();
                _started = true;
            }
        }
        _cond.wake_one();
    }
    return 0;
}

int itimers::get(int which, struct itimerval *curr_value)
{
    int idx = which_index(which);
    if (idx < 0 || !curr_value) {
        return EINVAL;
    }
    WITH_LOCK(_mutex) {
        auto it = _by_process.find(current_process());
        if (it == _by_process.end()) {
            *curr_value = {};
        } else {
            fill_value(it->second->t[idx], curr_value);
        }
    }
    return 0;
}

// Cancel the current process's timers that the current thread armed.
void itimers::cancel_this_thread()
{
    if (!t_owns_itimer) {
        return;
    }
    t_owns_itimer = false;
    unsigned me = sched::thread::current()->id();
    WITH_LOCK(_mutex) {
        auto it = _by_process.find(current_process());
        if (it == _by_process.end()) {
            return;
        }
        for (auto &e : it->second->t) {
            if (e.owner_tid == me) {
                disarm(e);
                e.interval = std::chrono::nanoseconds::zero();
                e.owner_tid = 0;
            }
        }
        _cond.wake_one();
    }
}

#if CONF_fork
// Drop a process's timers when its address space goes away.
void itimers::release(process_key key)
{
    process_itimers *p = nullptr;
    WITH_LOCK(_mutex) {
        auto it = _by_process.find(key);
        if (it == _by_process.end()) {
            return;
        }
        p = it->second;
        _by_process.erase(it);
        for (auto &e : p->t) {
            disarm(e);
        }
        _cond.wake_one();
    }
    fork_arena::kernel_heap_scope kh;
    delete p;
}
#endif

void itimers::deliver(pid_t pid, int signum, unsigned owner_tid)
{
    kill(pid, signum);
    if (!owner_tid) {
        return;
    }
    // Interrupt a system call the arming thread sleeps in, unless its
    // process ignores the signal.
    sched::with_thread_by_id(owner_tid, [signum] (sched::thread *t) {
        if (!t) {
            return;
        }
        const struct sigaction *actions = signal_actions;
#if CONF_fork
        if (auto *a = fork_child_signal_actions_for(t->address_space())) {
            actions = a;
        }
#endif
        if (!is_sig_ign(actions[signum - 1])) {
            t->interrupted(true);
        }
    });
}

void itimers::work()
{
    sched::timer tmr(*sched::thread::current());
    while (true) {
        pid_t pid = 0;
        int signum = 0;
        unsigned owner_tid = 0;
        WITH_LOCK(_mutex) {
            if (_armed.empty()) {
                _cond.wait(_mutex);
                continue;
            }
            auto &e = *_armed.begin();
            auto now = osv::clock::uptime::now();
            if (e.due > now) {
                tmr.set(e.due);
                _cond.wait(_mutex, &tmr);
                tmr.cancel();
                continue;
            }
            _armed.erase(_armed.iterator_to(e));
            if (e.interval != decltype(e.interval)::zero()) {
                e.due = now + e.interval;
                _armed.insert(e);
            }
            pid = e.pid;
            signum = e.signum;
            owner_tid = e.owner_tid;
        }
        // The entry may be released once the lock is dropped; use the copies.
        deliver(pid, signum, owner_tid);
    }
}

static itimers g_itimers;

} // anonymous namespace

void cancel_this_thread_alarm()
{
    g_itimers.cancel_this_thread();
}

#if CONF_fork
// Called when a fork child's address space is torn down or replaced by
// execve(): its timers die with the process.
extern "C" void fork_release_process_itimers(mmu::address_space *as)
{
    g_itimers.release(as);
}
#endif

OSV_LIBC_API
unsigned int alarm(unsigned int seconds)
{
    unsigned int ret;
    struct itimerval old_value{}, new_value{};

    new_value.it_value.tv_sec = seconds;

    setitimer(ITIMER_REAL, &new_value, &old_value);

    ret = old_value.it_value.tv_sec;

    if ((ret == 0 && old_value.it_value.tv_usec) ||
        old_value.it_value.tv_usec >= 500000)
        ret++;

    return ret;
}

extern "C" OSV_LIBC_API
int setitimer(int which, const struct itimerval *new_value,
    struct itimerval *old_value)
{
    return g_itimers.set(which, new_value, old_value);
}

extern "C" OSV_LIBC_API
int getitimer(int which, struct itimerval *curr_value)
{
    return g_itimers.get(which, curr_value);
}

// A per-thread stack set with sigaltstack() to be used by
// build_signal_frame() when handling synchronous signals, most
// importantly SIGSEGV (asynchronous signals run in a new thread in OSv,
// so this alternative stack is irrelevant).  signal_stack_begin is the
// beginning of stack region, and signal_stack_size is the size of this
// region (we need to know both when stacks grow down).
// The stack region is not guaranteed to be specially aligned, so when
// build_signal_frame() uses it it might need to further snipped this region.
// If signal_stack == nullptr, there is no alternative signal stack for
// this thread.
static __thread void* signal_stack_begin;
static __thread size_t signal_stack_size;

OSV_LIBC_API
int sigaltstack(const stack_t *ss, stack_t *oss)
{
    if (oss) {
        if (signal_stack_begin) {
            // FIXME: we are supposed to set SS_ONSTACK if a signal
            // handler is currently running on this stack.
            oss->ss_flags = 0;
            oss->ss_sp = signal_stack_begin;
            oss->ss_size = signal_stack_size;
        } else {
            oss->ss_flags = SS_DISABLE;
        }
    }
    if (ss == nullptr) {
        // sigaltstack may be used with ss==nullptr for querying the
        // current state without changing it.
        return 0;
    }
    // FIXME: we're are supposed to check if a signal handler is currently
    // running on this stack, and if it is forbid changing it (EPERM).
    if (ss->ss_flags & SS_DISABLE) {
        signal_stack_begin = nullptr;
    } else if (ss->ss_flags != 0) {
        errno = EINVAL;
        return -1;
    } else {
        signal_stack_begin = ss->ss_sp;
        signal_stack_size = ss->ss_size;
    }
    return 0;
}

extern "C" OSV_LIBC_API
int signalfd(int fd, const sigset_t *mask, int flags)
{
    WARN_STUBBED();
    errno = ENOSYS;
    return -1;
}

extern "C" OSV_LIBC_API
int sigwaitinfo(const sigset_t *__restrict mask,
                           siginfo_t *__restrict si)
{
    int signo;

    int ret = sigwait(mask, &signo);

    if (si) {
        memset(si, 0, sizeof(*si));
        si->si_signo = signo;
        si->si_errno = ret;
    }

    if (ret) {
        errno = ret;
        return -1;
    }

    return signo;
}
