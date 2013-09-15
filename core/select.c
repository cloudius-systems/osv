/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <time.h>
#include <stdlib.h>
#include <errno.h>
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

    if ((nfds < 0) || (nfds > FD_SETSIZE+1)) {
        select_d("select() failed 1");
        errno = EINVAL;
        return -1;
    }

    /* Some programs call select() with nfds=0 in order to sleep */
    if (nfds == 0) {
        struct timespec ts = {};

        if (timeout == NULL) {
            errno = EINVAL;
            return -1;
        }

        ts.tv_sec = timeout->tv_sec;
        ts.tv_nsec = timeout->tv_usec * 1000;
        nanosleep(&ts, 0);

        return 0;
    }

    req = malloc(max_size);
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

    /* call poll() */
    error = poll(req, poll_fd_idx, timeout_ms);
    if (error < 0) {
        select_d("select() poll() failed");
        free(req);
        return -1;
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

        if (readfds) {
            if (req[i].revents & POLLIN) {
                FD_SET(req[i].fd, readfds);
                num_fds++;
            }
        }

        if (writefds) {
            if (req[i].revents & POLLOUT) {
                FD_SET(req[i].fd, writefds);
                num_fds++;
            }
        }

        if (req[i].revents & POLLERR) {
            if (exceptfds) {
                FD_SET(req[i].fd, exceptfds);
                num_fds++;
            }

            errno = EBADF;
            error = -1;
            break;
        }
    }

    free(req);

    if (error == -1)
        return -1;

    select_d("select() return: %d", num_fds);
    return num_fds;
}
