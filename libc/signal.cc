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

#ifdef __x86_64__
#include "tls-switch.hh"
#endif

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
    int woken = 0;

    unsigned sigidx = signo - 1;
    for (auto& t: waiters[sigidx]) {
        woken++;
        t->remote_thread_local_var<int>(thread_pending_signal) = signo;
        t->wake();
    }
    return woken;
}

void wait_for_signal(unsigned int sigidx)
{
    SCOPE_LOCK(waiters_mutex);
    waiters[sigidx].push_front(sched::thread::current());
}

void unwait_for_signal(sched::thread *t, unsigned int sigidx)
{
    SCOPE_LOCK(waiters_mutex);
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
    if (is_sig_dfl(signal_actions[sigidx])) {
        // Our default is to abort the process
        abort();
    } else if(!is_sig_ign(signal_actions[sigidx])) {
        arch::build_signal_frame(ef, siginfo, signal_actions[sigidx]);
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
    if (oldact) {
        *oldact = signal_actions[sigidx];
    }
    if (act) {
        signal_actions[sigidx] = *act;
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

OSV_LIBC_API
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
    if (sig < 0 || sig > (int)nsignals) {
        errno = EINVAL;
        return -1;
    }
    unsigned sigidx = sig - 1;
    if (is_sig_dfl(signal_actions[sigidx])) {
        // Our default is to power off.
        debug("Uncaught signal %d (\"%s\"). Powering off.\n",
                sig, strsignal(sig));
        osv::poweroff();
    } else if(!is_sig_ign(signal_actions[sigidx])) {
        if ((pid == OSV_PID) || (pid == 0) || (pid == -1)) {
            // This semantically means signalling everybody. So we will signal
            // every thread that is waiting for this.
            //
            // The thread does not expect the signal handler to still be delivered,
            // so if we wake up some folks (usually just the one waiter), we should
            // not continue processing.
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
        const auto sa = signal_actions[sigidx];
        auto t = sched::thread::make([=] {
            if (sa.sa_flags & SA_RESETHAND) {
                signal_actions[sigidx].sa_flags = 0;
                signal_actions[sigidx].sa_handler = SIG_DFL;
            }
#ifdef __x86_64__
            //In case this signal handler thread has specified app thread local storage
            //let us switch to it before invoking the user handler routine
            arch::user_tls_switch tls_switch;
#endif
            if (sa.sa_flags & SA_SIGINFO) {
                // FIXME: proper second (siginfo) and third (context) arguments (See example in call_signal_handler)
                sa.sa_sigaction(sig, nullptr, nullptr);
            } else {
                sa.sa_handler(sig);
            }
        }, sched::thread::attr().detached().stack(65536).name("signal_handler"),
                false, true);
        //If we are running statically linked executable or a dynamic one with Linux
        //dynamic linker, its threads very likely use app thread local storage and signal
        //routine may rely on it presence. For that reason we use app TLS of the current
        //thread if it has one. Otherwise we find 1st app thread with non-null app TLS
        //and assign the signal handler thread app TLS to it so it is switched to (see above).
        //TODO: Ideally we should only run the logic below if the current app is statically
        //linked executable or a dynamic one ran with Linux dynamic linker
        //(what if we are handling "Ctrl-C"?)
        u64 app_tcb = sched::thread::current()->get_app_tcb();
        if (!app_tcb) {
            auto first_app_thread = sched::find_first_app_thread([&](sched::thread &t) {
                return t.get_app_tcb();
            });
            if (first_app_thread) {
                app_tcb = first_app_thread->get_app_tcb();
            }
        }
        if (app_tcb) {
            t->set_app_tcb(app_tcb);
        }
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
    : _alarm_thread(sched::thread::make([&] { work(); },
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
            get_interval(&old_value->it_interval);
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
        get_interval(&curr_value->it_interval);
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
                    kill(getpid(), _signum);
                    if(!is_sig_ign(signal_actions[_signum - 1])) {
                        _owner_thread->interrupted(true);
                    }
                } else {
#if CONF_lazy_stack_invariant
                    assert(!sched::thread::current()->is_app());
#endif
                    tmr.cancel();
                }
            } else {
                _cond.wait(_mutex);
            }
        }
    }
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

void itimer::cancel()
{
    _due = _no_alarm;
    _interval = decltype(_interval)::zero();
    if (!is_signal_handler()) {
        _owner_thread = nullptr;
    }
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
    if (!is_signal_handler()) {
        _owner_thread = sched::thread::current();
    }
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
    switch (which) {
    case ITIMER_REAL:
        return itimer_real.set(new_value, old_value);
    case ITIMER_VIRTUAL:
        return itimer_virt.set(new_value, old_value);
    default:
        return EINVAL;
    }
}

extern "C" OSV_LIBC_API
int getitimer(int which, struct itimerval *curr_value)
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
