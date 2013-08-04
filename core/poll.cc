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
#include <osv/debug.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/synch.h>
#include <bsd/sys/sys/queue.h>

#define dbg_d(...) tprintf_d("poll", __VA_ARGS__)

#include <osv/trace.hh>
TRACEPOINT(trace_poll_drain, "fd=%p", file*);
TRACEPOINT(trace_poll_wake, "fd=%p, events=0x%x", file*, int);
TRACEPOINT(trace_poll, "");


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
int poll_scan(struct pollfd _pfd[], nfds_t _nfds)
{
    dbg_d("poll_scan()");

    struct file* fp;
    struct pollfd* entry;
    int error;
    unsigned i;
    int nr_events = 0;

    for (i=0; i<_nfds; ++i) {
        entry = &_pfd[i];
        /* FIXME: verify zeroing revents is posix compliant */
        entry->revents = 0;

        error = fget(entry->fd, &fp);
        if (error) {
            entry->revents |= POLLERR;
            nr_events++;
            continue;
        }

        entry->revents = fo_poll(fp, entry->events);
        if (entry->revents) {
            nr_events++;
        }

        /*
         * POSIX requires POLLOUT to be never
         * set simultaneously with POLLHUP.
         */
        if ((entry->revents & POLLHUP) != 0)
            entry->revents &= ~POLLOUT;

        fdrop(fp);
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
            mtx_lock(&pl->_req->_awake_mutex);
            pl->_req->_awake = true;
            mtx_unlock(&pl->_req->_awake_mutex);
            wakeup((void*)pl->_req);
        }
    }

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
    int error;
    struct poll_link* pl;
    struct file* fp;
    struct pollfd* entry;

    dbg_d("poll_install()");

    for (i=0; i < p->_nfds; ++i) {
        entry = &p->_pfd[i];

        error = fget(entry->fd, &fp);
        assert(error == 0);

        /* Allocate a link */
        pl = (struct poll_link *) malloc(sizeof(struct poll_link));
        memset(pl, 0, sizeof(struct poll_link));

        /* Save a reference to request on current file structure.
         * will be cleared on wakeup()
         */
        pl->_req = p;
        pl->_events = entry->events;

        FD_LOCK(fp);
        TAILQ_INSERT_TAIL(&fp->f_poll_list, pl, _link);
        FD_UNLOCK(fp);
        // We need to check if we missed an event on this file just before
        // installing the poll request on it above.
        if(fo_poll(fp, entry->events)) {
            // Return immediately. poll() will call poll_scan() to get the
            // full list of events, and will call poll_uninstall() to undo
            // the partial installation we did here.
            mtx_lock(&p->_awake_mutex);
            p->_awake = true;
            mtx_unlock(&p->_awake_mutex);
            fdrop(fp);
            return;
        }
        fdrop(fp);
    }
}

void poll_uninstall(struct pollreq* p)
{
    unsigned i;
    int error;
    struct pollfd* entry_pfd;
    struct poll_link* pl;
    struct file* fp;

    dbg_d("poll_uninstall()");

    /* Remove pollreq from all file descriptors */
    for (i=0; i < p->_nfds; ++i) {
        entry_pfd = &p->_pfd[i];

        error = fget(entry_pfd->fd, &fp);
        if (error) {
            continue;
        }

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

        fdrop(fp);

    } /* End of clearing pollreq references from the other fds */
}

int poll(struct pollfd _pfd[], nfds_t _nfds, int _timeout)
{
    int nr_events, error;
    int timeout;
    struct pollreq p = {0};
    size_t pfd_sz = sizeof(struct pollfd) * _nfds;

    if (_nfds > FDMAX) {
        errno = EINVAL;
        return -1;
    }

    trace_poll();

    p._nfds = _nfds;
    p._timeout = _timeout;
    p._pfd = (struct pollfd *) malloc(pfd_sz);
    memcpy(p._pfd, _pfd, pfd_sz);
    mtx_init(&p._awake_mutex, "poll awake", NULL, MTX_DEF);
    p._awake = false;

    /* Any existing events return immediately */
    nr_events = poll_scan(p._pfd, _nfds);
    if (nr_events) {
        memcpy(_pfd, p._pfd, pfd_sz);
        free(p._pfd);
        return nr_events;
    }

    /* Timeout -> Don't wait... */
    if (p._timeout == 0) {
        memcpy(_pfd, p._pfd, pfd_sz);
        free(p._pfd);
        return 0;
    }

    /* Add pollreq references */
    poll_install(&p);

    /* Timeout */
    if (p._timeout < 0) {
        timeout = 0;
    } else {
        /* Convert timeout of ms to hz */
        timeout = p._timeout*(hz/1000L);
    }

    /* Block  */
    mtx_lock(&p._awake_mutex);
    if (p._awake) {
        // poll_install already noticed a missed event, or we got one after
        // poll_install. Don't sleep, or we won't be woken again!
        error = 0;
    } else {
        error = msleep((void *)&p, &p._awake_mutex, 0, "poll", timeout);
    }
    mtx_unlock(&p._awake_mutex);
    if (error != EWOULDBLOCK) {
        nr_events = poll_scan(p._pfd, _nfds);
    } else {
        nr_events = 0;
    }

    /* Remove pollreq references */
    poll_uninstall(&p);

    /* return (copy-out) */
    memcpy(_pfd, p._pfd, pfd_sz);
    mtx_destroy(&p._awake_mutex);
    free(p._pfd);

    return nr_events;
}

#undef dbg_d
