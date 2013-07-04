#include "signal.hh"
#include <string.h>

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
    if (signum < 0 || signum > (int)nsignals) {
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
