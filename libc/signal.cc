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

using namespace osv::clock::literals;

namespace osv {

// we can't use have __thread sigset because of the constructor
__thread __attribute__((aligned(sizeof(sigset))))
    char thread_signal_mask[sizeof(sigset)];

// Let's ignore rt signals. For standard signals, signal(7) says order is
// unspecified over multiple deliveries, so we always record the last one.  It
// also relieves us of any need for locking, since it doesn't matter if the
// pending signal changes: returning any one is fine
__thread int thread_pending_signal;

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

sigset* thread_signals(sched::thread *t)
{
    return t->remote_thread_local_ptr<sigset>(&thread_signal_mask);
}

inline bool is_sig_dfl(const struct sigaction &sa) {
    return (!(sa.sa_flags & SA_SIGINFO) && sa.sa_handler == SIG_DFL);
}

inline bool is_sig_ign(const struct sigaction &sa) {
    return (!(sa.sa_flags & SA_SIGINFO) && sa.sa_handler == SIG_IGN);
}

typedef std::list<sched::thread *> thread_list;
static std::array<thread_list, nsignals> waiters;
mutex waiters_mutex;

int wake_up_signal_waiters(int signo)
{
    SCOPE_LOCK(waiters_mutex);
    int woken = 0;

    for (auto& t: waiters[signo]) {
        woken++;
        t->remote_thread_local_var<int>(thread_pending_signal) = signo;
        t->wake();
    }
    return woken;
}

void wait_for_signal(int signo)
{
    SCOPE_LOCK(waiters_mutex);
    waiters[signo].push_front(sched::thread::current());
}

void unwait_for_signal(sched::thread *t, int signo)
{
    SCOPE_LOCK(waiters_mutex);
    waiters[signo].remove(t);
}

void unwait_for_signal(int signo)
{
    unwait_for_signal(sched::thread::current(), signo);
}

void __attribute__((constructor)) signals_register_thread_notifier()
{
    sched::thread::register_exit_notifier(
        [](sched::thread *t) {
            sigset *set = thread_signals(t);
            if (!set->mask.any()) { return; }
            for (unsigned i = 0; i < nsignals; ++i) {
                if (set->mask.test(i)) {
                    unwait_for_signal(t, i);
                }
            }
        }
    );
}

void generate_signal(siginfo_t &siginfo, exception_frame* ef)
{
    if (pthread_self() && thread_signals()->mask[siginfo.si_signo]) {
        // FIXME: need to find some other thread to deliver
        // FIXME: the signal to.
        //
        // There are certainly no waiters for this, because since we
        // only deliver signals through this method directly, the thread
        // needs to be running to generate them. So definitely not waiting.
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

int sigaction(int signum, const struct sigaction* act, struct sigaction* oldact)
{
    // FIXME: We do not support any sa_flags besides SA_SIGINFO.
    if (signum < 0 || signum >= (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
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

sighandler_t signal(int signum, sighandler_t handler)
{
    return signal(signum, handler, SA_RESTART);
}

extern "C"
sighandler_t __sysv_signal(int signum, sighandler_t handler)
{
    return signal(signum, handler, SA_RESETHAND | SA_NODEFER);
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

int sigwait(const sigset_t *set, int *sig)
{
    sched::thread::wait_until([sig] { return *sig = thread_pending_signal; });
    thread_pending_signal = 0;
    return 0;
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
        // Our default is to power off.
        debug("Uncaught signal %d (\"%s\"). Powering off.\n",
                sig, strsignal(sig));
        osv::poweroff();
    } else if(!is_sig_ign(signal_actions[sig])) {
        if ((pid == 0) || (pid == -1)) {
            // That semantically means signalling everybody (or that, or the
            // user did getpid() and got 0, all the same. So we will signal
            // every thread that is waiting for this.
            //
            // The thread does not expect the signal handler to still be delivered,
            // so if we wake up some folks (usually just the one waiter), we should
            // not continue processing.
            //
            // FIXME: Maybe it could be a good idea for our getpid() to start
            // returning 1 so we can differentiate between those cases?
            if (wake_up_signal_waiters(sig)) {
                return 0;
            }
        }

        // User-defined signal handler. Run it in a new thread. This isn't
        // very Unix-like behavior, but if we assume that the program doesn't
        // care which of its threads handle the signal - why not just create
        // a completely new thread and run it there...
        const auto sa = signal_actions[sig];
        auto t = new sched::thread([=] {
            if (sa.sa_flags & SA_RESETHAND) {
                signal_actions[sig].sa_flags = 0;
                signal_actions[sig].sa_handler = SIG_DFL;
            }
            if (sa.sa_flags & SA_SIGINFO) {
                // FIXME: proper second (siginfo) and third (context) arguments (See example in call_signal_handler)
                sa.sa_sigaction(sig, nullptr, nullptr);
            } else {
                if (sa.sa_flags & SA_RESETHAND) {
                    signal_actions[sig].sa_flags = 0;
                    signal_actions[sig].sa_handler = SIG_DFL;
                }
                sa.sa_handler(sig);
            }
        }, sched::thread::attr().detached().stack(65536).name("signal_handler"));
        t->start();
    }
    return 0;
}

// Our alarm() implementation has one system-wide alarm-thread, which waits
// for the single timer (or instructions to change the timer) and sends
// SIGALRM when the timer expires.
// alarm() is an archaic Unix API and didn't age well. It should should never
// be used in new programs.

class itimer {
public:
    explicit itimer(int signum, const char *name);
    void cancel_this_thread();
    int set(const struct itimerval *new_value,
        struct itimerval *old_value);
    int get(struct itimerval *curr_value);

private:
    void work();

    // Fllowing functions doesn't take mutex, caller has responsibility
    // to take it
    void cancel();
    void set_interval(const struct timeval *tv);
    void set_value(const struct timeval *tv);
    void get_interval(struct timeval *tv);
    void get_value(struct timeval *tv);

    mutex _mutex;
    condvar _cond;
    sched::thread *_alarm_thread;
    sched::thread *_owner_thread = nullptr;
    const osv::clock::uptime::time_point _no_alarm {};
    osv::clock::uptime::time_point _due = _no_alarm;
    std::chrono::nanoseconds _interval;
    int _signum;
    bool _started = false;
};

static itimer itimer_real(SIGALRM, "itimer-real");
static itimer itimer_virt(SIGVTALRM, "itimer-virt");

itimer::itimer(int signum, const char *name)
    : _alarm_thread(new sched::thread([&] { work(); },
                    sched::thread::attr().name(name)))
    , _signum(signum)
{
}

void itimer::cancel_this_thread()
{
    if(_owner_thread == sched::thread::current()) {
        WITH_LOCK(_mutex) {
            if(_owner_thread == sched::thread::current()) {
                cancel();
            }
        }
    }
}

int itimer::set(const struct itimerval *new_value,
    struct itimerval *old_value)
{
    if (!new_value)
        return EINVAL;

    WITH_LOCK(_mutex) {
        if (old_value) {
            get_interval(&old_value->it_value);
            get_value(&old_value->it_value);
        }
        cancel();
        if (new_value->it_value.tv_sec || new_value->it_value.tv_usec) {
            set_interval(&new_value->it_interval);
            set_value(&new_value->it_value);
        }
     }
    return 0;
}

int itimer::get(struct itimerval *curr_value)
{
    WITH_LOCK(_mutex) {
        get_interval(&curr_value->it_value);
        get_value(&curr_value->it_value);
    }
    return 0;
}
 
void itimer::work()
{
    sched::timer tmr(*sched::thread::current());
    while (true) {
        WITH_LOCK(_mutex) {
            if (_due != _no_alarm) {
                tmr.set(_due);
                _cond.wait(_mutex, &tmr);
                if (tmr.expired()) {
                    if (_interval != decltype(_interval)::zero()) {
                        auto now = osv::clock::uptime::now();
                        _due = now + _interval;
                    } else {
                        _due = _no_alarm;
                    }
                    kill(0, _signum);
                    if(!is_sig_ign(signal_actions[_signum])) {
                        _owner_thread->interrupted(true);
                    }
                } else {
                    tmr.cancel();
                }
            } else {
                _cond.wait(_mutex);
            }
        }
    }
}

void itimer::cancel()
{
    _due = _no_alarm;
    _interval = decltype(_interval)::zero();
    _owner_thread = nullptr;
    _cond.wake_one();
}

void itimer::set_value(const struct timeval *tv)
{
    auto now = osv::clock::uptime::now();

    if (!_started) {
        _alarm_thread->start();
        _started = true;
    }
    _due = now + tv->tv_sec * 1_s + tv->tv_usec * 1_us;
    _owner_thread = sched::thread::current();
    _cond.wake_one();
}

void itimer::set_interval(const struct timeval *tv)
{
    _interval = tv->tv_sec * 1_s + tv->tv_usec * 1_us;
}

void itimer::get_value(struct timeval *tv)
{
    if (_due == _no_alarm) {
        tv->tv_sec = tv->tv_usec = 0;
    } else {
        auto now = osv::clock::uptime::now();
        fill_tv(_due - now, tv);
    }
}

void itimer::get_interval(struct timeval *tv)
{
    fill_tv(_interval, tv);
}

void cancel_this_thread_alarm()
{
    itimer_real.cancel_this_thread();
    itimer_virt.cancel_this_thread();
}

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

extern "C" int setitimer(int which, const struct itimerval *new_value,
    struct itimerval *old_value)
{
    switch (which) {
    case ITIMER_REAL:
        return itimer_real.set(new_value, old_value);
    case ITIMER_VIRTUAL:
        return itimer_virt.set(new_value, old_value);
    default:
        return EINVAL;
    }
}

extern "C" int getitimer(int which, struct itimerval *curr_value)
{
    switch (which) {
    case ITIMER_REAL:
        return itimer_real.get(curr_value);
    case ITIMER_VIRTUAL:
        return itimer_virt.get(curr_value);
    default:
        return EINVAL;
    }
}

int sigaltstack(const stack_t *ss, stack_t *oss)
{
    WARN_STUBBED();
    return 0;
}

extern "C" int __sigsetjmp(sigjmp_buf env, int savemask)
{
    WARN_STUBBED();
    return 0;
}
