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
#include <debug.hh>
#include <osv/printf.hh>
#include <sched.hh>

namespace osv {

// we can't use have __thread sigset because of the constructor
__thread __attribute__((aligned(sizeof(sigset))))
    char thread_signal_mask[sizeof(sigset)];

struct sigaction signal_actions[nsignals];

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

inline bool is_sig_dfl(const struct sigaction &sa) {
    return (!(sa.sa_flags & SA_SIGINFO) && sa.sa_handler == SIG_DFL);
}

inline bool is_sig_ign(const struct sigaction &sa) {
    return (!(sa.sa_flags & SA_SIGINFO) && sa.sa_handler == SIG_IGN);
}

void generate_signal(siginfo_t &siginfo, exception_frame* ef)
{
    if (pthread_self() && thread_signals()->mask[siginfo.si_signo]) {
        // FIXME: need to find some other thread to deliver
        // FIXME: the signal to
        abort();
    }
    if (is_sig_dfl(signal_actions[siginfo.si_signo])) {
        // Our default is to abort the process
        abort();
    } else if(!is_sig_ign(signal_actions[siginfo.si_signo])) {
        arch::build_signal_frame(ef, siginfo, signal_actions[siginfo.si_signo]);
    }
}

void handle_segmentation_fault(ulong addr, exception_frame* ef)
{
    siginfo_t si;
    si.si_signo = SIGSEGV;
    si.si_addr = reinterpret_cast<void*>(addr);
    generate_signal(si, ef);
}

}

using namespace osv;

int sigemptyset(sigset_t* sigset)
{
    from_libc(sigset)->mask.reset();
    return 0;
}

int sigfillset(sigset_t *sigset)
{
    from_libc(sigset)->mask.set();
    return 0;
}

int sigaddset(sigset_t *sigset, int signum)
{
    from_libc(sigset)->mask.set(signum);
    return 0;
}

int sigdelset(sigset_t *sigset, int signum)
{
    from_libc(sigset)->mask.reset(signum);
    return 0;
}

int sigismember(const sigset_t *sigset, int signum)
{
    return from_libc(sigset)->mask.test(signum);
}

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
            thread_signals()->mask |= set->mask;
            break;
        case SIG_UNBLOCK:
            thread_signals()->mask &= ~set->mask;
            break;
        case SIG_SETMASK:
            thread_signals()->mask = set->mask;
            break;
        default:
            abort();
        }
    }
    return 0;
}

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    if (oldact) {
        *oldact = signal_actions[signum];
    }
    if (act) {
        signal_actions[signum] = *act;
    }
    return 0;
}

// using signal() is not recommended (use sigaction instead!), but some
// programs like to call to do simple things, like ignoring a certain signal.
sighandler_t signal(int signum, sighandler_t handler)
{
    if (signum < 0 || signum >= (int)nsignals) {
        errno = EINVAL;
        return SIG_ERR;
    }
    struct sigaction old = signal_actions[signum];
    memset(&signal_actions[signum], 0, sizeof(signal_actions[signum]));
    signal_actions[signum].sa_handler = handler;
    if (old.sa_flags & SA_SIGINFO) {
        // TODO: Is there anything sane to do here?
        return nullptr;
    } else {
        return old.sa_handler;
    }
}

// using sigignore() and friends is not recommended as it is obsolete System V
// APIs. Nevertheless, some programs use it.
int sigignore(int signum)
{
    struct sigaction act;
    act.sa_flags = 0;
    sigemptyset(&act.sa_mask);
    act.sa_handler = SIG_IGN;
    return sigaction(signum, &act, nullptr);
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

int kill(pid_t pid, int sig)
{
    // OSv only implements one process, whose pid is getpid().
    // Sending a signal to pid 0 or -1 is also fine, as it will also send a
    // signal to the same single process.
    if (pid != getpid() && pid != 0 && pid != -1) {
        errno = ESRCH;
        return -1;
    }
    if (sig == 0) {
        // kill() with signal 0 doesn't cause an actual signal 0, just
        // testing the pid.
        return 0;
    }
    if (sig < 0 || sig >= (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    if (is_sig_dfl(signal_actions[sig])) {
        // Our default is to abort the process
        abort(osv::sprintf("Uncaught signal %d (\"%s\"). Aborting.\n",
                sig, strsignal(sig)).c_str());
    } else if(!is_sig_ign(signal_actions[sig])) {
        // User-defined signal handler. Run it in a new thread. This isn't
        // very Unix-like behavior, but if we assume that the program doesn't
        // care which of its threads handle the signal - why not just create
        // a completely new thread and run it there...
        const auto& sa = signal_actions[sig];
        sched::thread::attr a;
        a.detached = true;
        a.stack.size = 65536; // TODO: what is a good size?
        auto t = new sched::thread([=] {
            if (sa.sa_flags & SA_SIGINFO) {
                // FIXME: proper second (siginfo) and third (context) arguments (See example in call_signal_handler)
                sa.sa_sigaction(sig, nullptr, nullptr);
            } else {
                sa.sa_handler(sig);
            }
        }, a);
        t->start();
    }
    return 0;
}
