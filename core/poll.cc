/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*-
 * Copyright (c) 1982, 1986, 1989, 1993
 *  The Regents of the University of California.  All rights reserved.
 * (c) UNIX System Laboratories, Inc.
 * All or some portions of this file are derived from material licensed
 * to the University of California by American Telephone and Telegraph
 * Co. or Unix System Laboratories, Inc. and are reproduced herein with
 * the permission of UNIX System Laboratories, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
#include <inttypes.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>

#include <osv/file.h>
#include <osv/poll.h>
#include <sys/epoll.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/synch.h>
#include <bsd/sys/sys/queue.h>

#define dbg_d(...) tprintf_d("poll", __VA_ARGS__)

#include <osv/trace.hh>
TRACEPOINT(trace_poll_drain, "fd=%p", file*);
TRACEPOINT(trace_poll_wake, "fd=%p, events=0x%x", file*, int);
TRACEPOINT(trace_poll, "_pfd=%p, _nfds=%lu, _timeout=%d", struct pollfd *, nfds_t, int);
TRACEPOINT(trace_poll_ret, "%d", int);
TRACEPOINT(trace_poll_err, "%d", int);

using namespace std;

int poll_no_poll(int events)
{
    /*
     * Return true for read/write.  If the user asked for something
     * special, return POLLNVAL, so that clients have a way of
     * determining reliably whether or not the extended
     * functionality is present without hard-coding knowledge
     * of specific filesystem implementations.
     */
    if (events & ~POLLSTANDARD)
        return (POLLNVAL);

    return (events & (POLLIN | POLLOUT | POLLRDNORM | POLLWRNORM));
}

/* Drain the poll link list from the file... */
void poll_drain(struct file* fp)
{
    trace_poll_drain(fp);

    FD_LOCK(fp);
    while (!TAILQ_EMPTY(&fp->f_poll_list)) {
        struct poll_link *pl = TAILQ_FIRST(&fp->f_poll_list);

        /* FIXME: TODO -
         * Should we mark POLLHUP?
         * Should we wake the pollers?
         */

        TAILQ_REMOVE(&fp->f_poll_list, pl, _link);
        free(pl);
    }
    FD_UNLOCK(fp);
}


/*
 * Iterate all file descriptors and search for existing events,
 * Fill-in the revents for each fd in the poll.
 *
 * Returns the number of file descriptors changed
 */
int poll_scan(std::vector<poll_file>& _pfd)
{
    dbg_d("poll_scan()");

    int nr_events = 0;

    for (auto& e : _pfd) {
        auto* entry = &e;
        entry->revents = 0;

        auto* fp = entry->fp.get();
        if (!fp) {
            entry->revents |= POLLNVAL;
            nr_events++;
            continue;
        }

        entry->revents = fp->poll(entry->events);

        // Hack for implementing epoll() over poll(). Note we can't avoid the
        // above fp->poll() call, because sockets have the SB_SEL optimization
        // where only their poll (so_poll_generic()) turns on SB_SEL.
        if (entry->events & EPOLLET) {
            WITH_LOCK(fp->f_lock) {
                if (entry->last_poll_wake_count == fp->poll_wake_count) {
                    entry->revents = 0;
                    continue;
                }
                entry->last_poll_wake_count = fp->poll_wake_count;
            }
        }

        if (entry->revents) {
            nr_events++;
        }

        /*
         * POSIX requires POLLOUT to be never
         * set simultaneously with POLLHUP.
         */
        if ((entry->revents & POLLHUP) != 0)
            entry->revents &= ~POLLOUT;
    }

    return nr_events;
}

/*
 * Signal the file descriptor with changed events
 * This function is invoked when the file descriptor is changed.
 */
int poll_wake(struct file* fp, int events)
{
    struct poll_link *pl;

    if (!fp)
        return 0;

    trace_poll_wake(fp, events);

    fhold(fp);

    FD_LOCK(fp);
    /*
     * There may be several pollreqs associated with this fd.
     * Wake each and every one.
     */
    TAILQ_FOREACH(pl, &fp->f_poll_list, _link) {
        if (pl->_events & events) {
            pl->_req->_awake.store(true, memory_order_relaxed);
            pl->_req->_poll_thread.wake();
        }
    }

    // poll_wake_count is used for implementing epoll()'s EPOLLET
    // over regular poll().
    ++fp->poll_wake_count;

    FD_UNLOCK(fp);
    fdrop(fp);

    return 0;
}

/*
  * Add a pollreq reference to all file descriptors that participate
  * The reference is added via struct poll_link
  *
  * Multiple poll requests can be issued on the same fd, so we manage
  * the references in a linked list...
  */
void poll_install(struct pollreq* p)
{
    unsigned i;
    struct poll_link* pl;
    struct poll_file* entry;

    dbg_d("poll_install()");

    for (i=0; i < p->_nfds; ++i) {
        entry = &p->_pfd[i];

        auto fp = entry->fp.get();
        assert(fp);

        /* Allocate a link */
        pl = (struct poll_link *) malloc(sizeof(struct poll_link));
        memset(pl, 0, sizeof(struct poll_link));

        /* Save a reference to request on current file structure.
         * will be cleared on wakeup()
         */
        pl->_req = p;
        // In addition to the user's requested events, also allow events which
        // cannot be requested by the user (e.g., POLLHUP).
        pl->_events = entry->events | ~POLL_REQUESTABLE;

        fp->poll_install(*p);
        FD_LOCK(fp);
        TAILQ_INSERT_TAIL(&fp->f_poll_list, pl, _link);
        if (entry->events & EPOLLET &&
                entry->last_poll_wake_count == fp->poll_wake_count) {
            // In this case, don't use fp->poll() to check for missed event
            FD_UNLOCK(fp);
            continue;
        }
        FD_UNLOCK(fp);
        // We need to check if we missed an event on this file just before
        // installing the poll request on it above.
        if(fp->poll(entry->events)) {
            // Return immediately. poll() will call poll_scan() to get the
            // full list of events, and will call poll_uninstall() to undo
            // the partial installation we did here.
            p->_awake.store(true, memory_order_relaxed);
            return;
        }
    }
}

void poll_uninstall(struct pollreq* p)
{
    unsigned i;
    struct poll_link* pl;

    dbg_d("poll_uninstall()");

    /* Remove pollreq from all file descriptors */
    for (i=0; i < p->_nfds; ++i) {
        auto entry = &p->_pfd[i];

        auto fp = entry->fp.get();
        if (!fp) {
            continue;
        }

        fp->poll_uninstall(*p);
        /* Search for current pollreq and remove it from list */
        FD_LOCK(fp);
        TAILQ_FOREACH(pl, &fp->f_poll_list, _link) {
            if (pl->_req == p) {
                TAILQ_REMOVE(&fp->f_poll_list, pl, _link);
                free(pl);
                break;
            }
        }
        FD_UNLOCK(fp);

    } /* End of clearing pollreq references from the other fds */
}

int do_poll(std::vector<poll_file>& pfd, int _timeout)
{
    int nr_events;
    unique_ptr<pollreq> p{new pollreq};
    sched::timer tmr(*sched::thread::current());

    p->_nfds = pfd.size();
    p->_timeout = _timeout;
    p->_pfd = std::move(pfd);

    /* Any existing events return immediately */
    nr_events = poll_scan(p->_pfd);
    if (nr_events) {
        pfd = std::move(p->_pfd);
        goto out;
    }

    /* Timeout -> Don't wait... */
    if (p->_timeout == 0) {
        pfd = std::move(p->_pfd);
        goto out;
    }

    /* Add pollreq references */
    poll_install(p.get());

    /* Timeout */
    if (p->_timeout > 0) {
        /* Convert timeout of ms to ns */
        using namespace osv::clock::literals;
        tmr.set(p->_timeout * 1_ms);
    }

    /* Block  */
    do {
        sched::thread::wait_until([&] {
            return p->_awake.load(memory_order_relaxed) || tmr.expired();
        });

        nr_events = 0;
        if (p->_awake.load(memory_order_relaxed)) {
            p->_awake.store(false, memory_order_relaxed);
            nr_events = poll_scan(p->_pfd);
        }
    } while (!nr_events && !tmr.expired());

    /* Remove pollreq references */
    poll_uninstall(p.get());

    pfd = std::move(p->_pfd);
out:
    p->_poll_thread.clear();
    osv::rcu_dispose(p.release());
    return nr_events;
}

int poll(struct pollfd _pfd[], nfds_t _nfds, int _timeout)
{
    trace_poll(_pfd, _nfds, _timeout);

    if (_nfds > FDMAX) {
        errno = EINVAL;
        trace_poll_err(errno);
        return -1;
    }

    std::vector<poll_file> pfd;
    pfd.reserve(_nfds);

    for (nfds_t i = 0; i < _nfds; ++i) {
        pfd.emplace_back(fileref_from_fd(_pfd[i].fd), _pfd->events, _pfd->revents);
    }

    auto ret = do_poll(pfd, _timeout);

    for (nfds_t i = 0; i < pfd.size(); ++i) {
        _pfd[i].revents = pfd[i].revents;
    }

    trace_poll_ret(ret);
    return ret;
}

/* Used by code compiled on Linux with -D_FORTIFY_SOURCE */
extern "C"
int __poll_chk (struct pollfd _pfd[], nfds_t _nfds, int _timeout, size_t pdflen)
{
  assert(pdflen / sizeof (_pfd[0]) >= _nfds);
  return poll (_pfd, _nfds, _timeout);
}

#undef dbg_d
