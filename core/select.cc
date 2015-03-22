/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <signal.h>
#include <osv/poll.h>
#include <osv/debug.h>
#include <api/sys/select.h>
#include <bsd/porting/synch.h>

#define select_d(...)  tprintf_d("select", __VA_ARGS__)

/* Basic select() implementation on top of poll() */
int select (int nfds,
            fd_set * readfds,
            fd_set * writefds,
            fd_set * exceptfds,
            struct timeval * timeout)
{
    int timeout_ms, error, i;
    int poll_fd_idx = 0;
    int num_fds = 0;
    struct pollfd *req = NULL;
    size_t max_size = sizeof(struct pollfd) * nfds;

    select_d("select(nfds=%d, readfds=0x%lx, writefds=0x%lx, exceptfds=0x%lx, timeout=0x%lx)",
        nfds, readfds, writefds, exceptfds, timeout);

    if ((nfds < 0) || (nfds > FD_SETSIZE)) {
        select_d("select() failed 1");
        errno = EINVAL;
        return -1;
    }

    /* Some programs call select() with nfds=0 in order to sleep */
    if (nfds == 0) {
        struct timespec ts = {};

        if (timeout == NULL) {
            return pause();
        }

        ts.tv_sec = timeout->tv_sec;
        ts.tv_nsec = timeout->tv_usec * 1000;
        nanosleep(&ts, 0);

        return 0;
    }

    req = static_cast<pollfd*>(malloc(max_size));
    if (req == NULL) {
        select_d("select() failed no memory");
        errno = ENOMEM;
        return -1;
    }

    /* prepare poll request */
    for (i=0; i < nfds; i++) {
        short int events = 0;

        if (readfds) {
            /* read_fds */
            if (FD_ISSET(i, readfds))
                events = POLLIN;
        }

        if (writefds) {
            /* write_fds */
            if (FD_ISSET(i, writefds))
                events |= POLLOUT;
        }

        if (exceptfds) {
            /* exception_fds */
            if (FD_ISSET(i, exceptfds))
                events |= POLLPRI;
        }


        if (events) {
            req[poll_fd_idx].fd = i;
            req[poll_fd_idx].events = events;
            req[poll_fd_idx].revents = 0;

            poll_fd_idx++;
        }
    }

    if (timeout) {
        /* FIXME: we use a ms granularity while select uses timeval (microsec) */
        timeout_ms = timeout->tv_sec * 1000 + timeout->tv_usec/1000;
    } else {
        timeout_ms = -1;
    }

    select_d("select() timeout_ms=%d", timeout_ms);

    osv::clock::uptime::time_point time_before_poll;
    if (timeout) {
        time_before_poll = osv::clock::uptime::now();
    }

    /* call poll() */
    error = poll(req, poll_fd_idx, timeout_ms);
    if (error < 0) {
        select_d("select() poll() failed");
        free(req);
        return -1;
    }

    /* update timeout to be the amount of time not slept (to be consistent
     * with Linux select() behavior)
     */
    if (timeout) {
        auto time_after_poll = osv::clock::uptime::now();
        auto time_slept_us = std::chrono::duration_cast<std::chrono::microseconds>(time_after_poll - time_before_poll).count();
        struct timeval time_slept = { .tv_sec  = time_slept_us / 1000000,
                                      .tv_usec = time_slept_us % 1000000 };
        timersub(timeout, &time_slept, timeout);
        if (timeout->tv_sec < 0) {
            timeout->tv_sec = 0;
            timeout->tv_usec = 0;
        }
    }

    /* handle result */
    if (readfds)
        FD_ZERO(readfds);

    if (writefds)
        FD_ZERO(writefds);

    if (exceptfds)
        FD_ZERO(exceptfds);

    for (i=0; i < poll_fd_idx; i++) {
        if (req[i].revents == 0)
            continue;
        if (req[i].revents & POLLNVAL) {
            errno = EBADF;
            error = -1;
            break;
        }
        bool event = false;
        if (readfds) {
            if (req[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                FD_SET(req[i].fd, readfds);
                event = true;
            }
        }

        if (writefds) {
            if (req[i].revents & (POLLOUT | POLLRDHUP | POLLERR)) {
                FD_SET(req[i].fd, writefds);
                event = true;
            }
        }

        if (exceptfds) {
            if (req[i].revents & POLLPRI) {
                FD_SET(req[i].fd, exceptfds);
                event = true;
            }
        }

        if (event) {
            num_fds++;
        }
    }

    free(req);

    if (error == -1)
        return -1;

    select_d("select() return: %d", num_fds);
    return num_fds;
}

/* Basic pselect() on top of select() */
int pselect(int nfds, fd_set *readfds, fd_set *writefds,
            fd_set *exceptfds, const struct timespec *timeout_ts,
            const sigset_t *sigmask)
{
    sigset_t origmask;
    struct timeval timeout;

    if (timeout_ts) {
        timeout.tv_sec = timeout_ts->tv_sec;
        timeout.tv_usec = timeout_ts->tv_nsec / 1000;
    }

    sigprocmask(SIG_SETMASK, sigmask, &origmask);
    auto ret = select(nfds, readfds, writefds, exceptfds,
                                        timeout_ts == NULL? NULL : &timeout);
    sigprocmask(SIG_SETMASK, &origmask, NULL);
    return ret;
}

#define NFDBITS (8 * sizeof(fd_mask))

extern "C" unsigned long int
__fdelt_chk (unsigned long int d)
{
    if (d >= FD_SETSIZE)
        abort();
    return d / NFDBITS;
}
