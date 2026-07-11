/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// signalfd(2): deliver signals to a file descriptor instead of via a handler.
//
// OSv is a single process, so a signalfd watches a set of signals for the whole
// image.  When kill() raises a signal that some signalfd is watching, the
// signal is queued to those fds (and their pollers woken) instead of running
// the default action or a handler -- matching Linux, where a signalfd-consumed
// signal must be blocked and is dequeued through the fd.

#include <sys/signalfd.h>
#include <fs/fs.hh>
#include <libc/libc.hh>
#include <osv/fcntl.h>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/poll.h>
#include <osv/signal.hh>

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

    void set_mask(const sigset_t& mask) { _mask = mask; }

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
        while (!_queue.empty() &&
               uio->uio_resid >= (ssize_t)sizeof(signalfd_siginfo)) {
            signalfd_siginfo si = _queue.front();
            _queue.pop_front();
            int err = uiomove(&si, sizeof(si), uio);
            if (err) {
                return err;
            }
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

// Called by kill() (libc/signal.cc) when a signal is raised.  Returns true if at
// least one signalfd consumed the signal, in which case the caller must not run
// the default action or a handler.
bool osv_signalfd_deliver(int signo)
{
    signalfd_siginfo si;
    memset(&si, 0, sizeof(si));
    si.ssi_signo = signo;
    si.ssi_pid = getpid();

    bool delivered = false;
    WITH_LOCK(registry_lock) {
        for (auto* sfd : registry) {
            if (sfd->watches(signo)) {
                sfd->deliver(si);
                delivered = true;
            }
        }
    }
    return delivered;
}

extern "C" OSV_LIBC_API
int signalfd(int fd, const sigset_t* mask, int flags)
{
    if (!mask || (flags & ~(SFD_CLOEXEC | SFD_NONBLOCK))) {
        return libc_error(EINVAL);
    }

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
            fileref f = make_file<signal_fd>(*mask, of);
            fdesc newfd(f);
            return newfd.release();
        } catch (int error) {
            return libc_error(error);
        }
    }

    // Update the mask of an existing signalfd.
    fileref f(fileref_from_fd(fd));
    if (!f) {
        return libc_error(EBADF);
    }
    auto* sfd = dynamic_cast<signal_fd*>(f.get());
    if (!sfd) {
        return libc_error(EINVAL);
    }
    sfd->set_mask(*mask);
    return fd;
}
