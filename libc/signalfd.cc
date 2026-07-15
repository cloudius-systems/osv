/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// signalfd(2): deliver signals to a file descriptor instead of via a handler.
//
// OSv is a single process, so a signalfd watches a set of signals for the whole
// image.  When kill() raises a signal that some signalfd is watching, the
// signal is queued to a single (arbitrary) watching fd (and its pollers woken)
// instead of running the default action or a handler.  This approximates Linux,
// where a blocked signal is consumed by one reader rather than broadcast to
// every signalfd; the fd chosen here is arbitrary (registry iteration order),
// and the case of two signalfds watching the same signal diverges from Linux's
// per-thread pending-set accounting.
//
// Note on Linux fidelity: on Linux a signal is consumed by a signalfd only
// when it is blocked in the caller's signal mask; an unblocked signal still
// runs its handler/default action.  OSv's signal model is already a
// simplification (see libc/signal.cc - kill() runs the handler on a fresh
// thread rather than a designated one), and it does not track a precise
// per-thread blocked mask here, so a watched signal is consumed by the
// signalfd unconditionally.  This is sufficient for the common use (an app
// blocks the signals it hands to a signalfd and reads them through the fd) but
// is not bit-for-bit Linux semantics.  SIGKILL and SIGSTOP are filtered from
// the watched mask so they always keep their uncatchable default behavior.

#include <sys/signalfd.h>
#include <fs/fs.hh>
#include <libc/libc.hh>
#include <osv/fcntl.h>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/poll.h>
#include <osv/signal.hh>

#include <unistd.h>
#include <string.h>

#include <deque>
#include <set>
#include <signal.h>
#include <errno.h>

namespace {

class signal_fd;

// Registry of all live signalfd objects, so kill() can find the ones watching a
// given signal.  Guarded by its own lock.
mutex registry_lock;
std::set<signal_fd*> registry;

class signal_fd final : public special_file {
public:
    signal_fd(const sigset_t& mask, int flags)
        : special_file(FREAD | flags, DTYPE_UNSPEC), _mask(mask)
    {
        WITH_LOCK(registry_lock) { registry.insert(this); }
    }

    int close() override {
        WITH_LOCK(registry_lock) { registry.erase(this); }
        return 0;
    }

    int read(struct uio* uio, int flags) override;
    int poll(int events) override;

    // Is this fd watching signal `signo` (1-based)?
    bool watches(int signo) const {
        return sigismember(&_mask, signo) == 1;
    }

    // Called from the signal path to enqueue a delivered signal.
    void deliver(const signalfd_siginfo& si) {
        WITH_LOCK(_mutex) {
            _queue.push_back(si);
            _blocked_reader.wake_all();
        }
        poll_wake(this, POLLIN);
    }

    // Update the watched-signal set.  Taken under registry_lock (the same lock
    // held while the signal-dispatch path calls watches()), so a concurrent
    // signalfd() mask update cannot race a delivery's read of _mask.
    void set_mask(const sigset_t& mask) {
        WITH_LOCK(registry_lock) { _mask = mask; }
    }

private:
    mutable mutex _mutex;
    sigset_t      _mask;
    condvar       _blocked_reader;
    std::deque<signalfd_siginfo> _queue;
};

int signal_fd::read(struct uio* uio, int flags)
{
    if (uio->uio_resid < (ssize_t)sizeof(signalfd_siginfo)) {
        return EINVAL;
    }

    WITH_LOCK(_mutex) {
        while (_queue.empty()) {
            if (f_flags & FNONBLOCK) {
                return EAGAIN;
            }
            _blocked_reader.wait(_mutex);
        }
        // Copy out as many queued siginfos as fit in the caller's buffer.
        // Peek, copy, and only dequeue after a successful copy so a failed
        // copy-out never silently drops a queued signal.  (OSv's uiomove uses
        // memcpy and does not currently fail, but keep the ordering correct.)
        while (!_queue.empty() &&
               uio->uio_resid >= (ssize_t)sizeof(signalfd_siginfo)) {
            signalfd_siginfo si = _queue.front();
            int err = uiomove(&si, sizeof(si), uio);
            if (err) {
                // If nothing was copied yet, report the error; otherwise the
                // bytes already copied are a valid short read.
                return err;
            }
            _queue.pop_front();
        }
    }
    return 0;
}

int signal_fd::poll(int events)
{
    int rc = 0;
    WITH_LOCK(_mutex) {
        if (!_queue.empty() && (events & POLLIN)) {
            rc |= POLLIN;
        }
    }
    return rc;
}

} // anonymous namespace

// Called by kill() (libc/signal.cc) when a signal is raised.  Returns true if a
// signalfd consumed the signal (queued to exactly one arbitrary watching fd),
// in which case the caller must not run the default action or a handler.
bool osv_signalfd_deliver(int signo)
{
    signalfd_siginfo si;
    memset(&si, 0, sizeof(si));
    si.ssi_signo = signo;
    si.ssi_pid = getpid();

    // Deliver to a single (arbitrary) watching signalfd: approximate Linux,
    // where a blocked signal is consumed by one reader rather than broadcast to
    // every signalfd.  Stop at the first match (registry iteration order).
    WITH_LOCK(registry_lock) {
        for (auto* sfd : registry) {
            if (sfd->watches(signo)) {
                sfd->deliver(si);
                return true;
            }
        }
    }
    return false;
}

extern "C" OSV_LIBC_API
int signalfd(int fd, const sigset_t* mask, int flags)
{
    if (!mask || (flags & ~(SFD_CLOEXEC | SFD_NONBLOCK))) {
        return libc_error(EINVAL);
    }

    // SIGKILL and SIGSTOP cannot be caught, blocked, or consumed via a
    // signalfd (as with sigprocmask); drop them from the effective mask so they
    // always retain their default (uncatchable) behavior rather than being
    // silently swallowed by a signalfd reader.
    sigset_t effective = *mask;
    sigdelset(&effective, SIGKILL);
    sigdelset(&effective, SIGSTOP);

    int of = 0;
    if (flags & SFD_NONBLOCK) {
        of |= O_NONBLOCK;
    }
    if (flags & SFD_CLOEXEC) {
        of |= O_CLOEXEC;
    }

    if (fd == -1) {
        // Create a new signalfd.
        try {
            fileref f = make_file<signal_fd>(effective, of);
            fdesc newfd(f);
            return newfd.release();
        } catch (int error) {
            return libc_error(error);
        }
    }

    // Update the mask of an existing signalfd.  (Per the signalfd(2) man page
    // the flags argument is only meaningful when creating a new fd, so it is
    // not re-applied here.)
    fileref f(fileref_from_fd(fd));
    if (!f) {
        return libc_error(EBADF);
    }
    auto* sfd = dynamic_cast<signal_fd*>(f.get());
    if (!sfd) {
        return libc_error(EINVAL);
    }
    sfd->set_mask(effective);
    return fd;
}
