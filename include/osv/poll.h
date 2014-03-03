/*-
 * Copyright (c) 1997 Peter Wemm <peter@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _OSV_POLL_H_
#define _OSV_POLL_H_

#include <sys/cdefs.h>
#include <bsd/sys/sys/queue.h>
#include <stdbool.h>
#include <bsd/porting/sync_stub.h>

#ifdef __cplusplus

#include <fs/fs.hh>
#include <vector>

#endif

__BEGIN_DECLS

/*
 * This file is intended to be compatible with the traditional poll.h.
 */


#if 0  // but it conflicts with <sys/poll.h>

typedef unsigned int    nfds_t;

/*
 * This structure is passed as an array to poll(2).
 */
struct pollfd {
    int fd;     /* which file descriptor to poll */
    short   events;     /* events we are interested in */
    short   revents;    /* events found on return */
};

/*
 * Requestable events.  If poll(2) finds any of these set, they are
 * copied to revents on return.
 * XXX Note that FreeBSD doesn't make much distinction between POLLPRI
 * and POLLRDBAND since none of the file types have distinct priority
 * bands - and only some have an urgent "mode".
 * XXX Note POLLIN isn't really supported in true SVSV terms.  Under SYSV
 * POLLIN includes all of normal, band and urgent data.  Most poll handlers
 * on FreeBSD only treat it as "normal" data.
 */
#define POLLIN      0x0001      /* any readable data available */
#define POLLPRI     0x0002      /* OOB/Urgent readable data */
#define POLLOUT     0x0004      /* file descriptor is writeable */
#define POLLRDNORM  0x0040      /* non-OOB/URG data available */
#define POLLWRNORM  POLLOUT     /* no write type differentiation */
#define POLLRDBAND  0x0080      /* OOB/Urgent readable data */
#define POLLWRBAND  0x0100      /* OOB/Urgent data can be written */

/* General FreeBSD extension (currently only supported for sockets): */
#define POLLINIGNEOF    0x2000      /* like POLLIN, except ignore EOF */

/*
 * These events are set if they occur regardless of whether they were
 * requested.
 */
#define POLLERR     0x0008      /* some poll error occurred */
#define POLLHUP     0x0010      /* file descriptor was "hung up" */
#define POLLNVAL    0x0020      /* requested events "invalid" */
#define POLLRDHUP   0x2000      /* Reader hung up */

#define POLLSTANDARD    (POLLIN|POLLPRI|POLLOUT|POLLRDNORM|POLLRDBAND|\
             POLLWRBAND|POLLERR|POLLHUP|POLLNVAL)

/*
 * Request that poll() wait forever.
 */
#define INFTIM      (-1)

#else

#include <sys/poll.h>
#define POLLSTANDARD    (POLLIN|POLLPRI|POLLOUT|POLLRDNORM|POLLRDBAND|\
             POLLWRBAND|POLLERR|POLLHUP|POLLNVAL)
/* General FreeBSD extension (currently only supported for sockets): */
#define POLLINIGNEOF    0x2000      /* like POLLIN, except ignore EOF */

#endif

/*
 * The bits in POLL_REQUESTABLE are allowed in poll's input "events" bitmask,
 * and therefore will not be returned in "revents" unless they are also turned
 * on in "events". Bits outside POLL_REQUESTABLE, such as POLLHUP, POLLERR or
 * POLLNVAL, cannot be requested, but may still be returned in "revents".
 */
#define POLL_REQUESTABLE (POLLIN | POLLOUT | POLLPRI | POLLRDNORM | \
            POLLWRNORM | POLLRDBAND | POLLWRBAND)

struct poll_file;

#ifdef __cplusplus

struct poll_file {
    poll_file() = default;
    // Note that events is int, not short, for EPOLLET support.
    poll_file(fileref fp, int events, short revents, int c = 0)
        : fp(fp), events(events), revents(revents), last_poll_wake_count(c) {}
    fileref fp;
    int events;
    short revents;
    int last_poll_wake_count;   // For implementing EPOLLET
};

/*
 * Each file descriptor saves a reference to an allocated poll request.
 * This structure is allocated when poll() is called and deallocated when
 * poll() is returned.
 *
 * We allocate this structure so the user won't mess with this data after poll()
 * is issued.
 *
 */
struct pollreq {
    std::vector<poll_file> _pfd;
    nfds_t _nfds;
    int _timeout;
    std::atomic<bool> _awake = { false };
    sched::thread_handle _poll_thread = { *sched::thread::current() };
};

#endif

/* linked list of pollreq links */
struct poll_link {
    TAILQ_ENTRY(poll_link) _link;
    struct pollreq* _req;
    /* Events being polled... */
    int _events;
};

struct file;

int poll_wake(struct file* fp, int events);
int poll(struct pollfd _pfd[], nfds_t _nfds, int _timeout);
void poll_drain(struct file* fp);
int poll_no_poll(int events);
__END_DECLS

#ifdef __cplusplus

int do_poll(std::vector<poll_file>& pfd, int _timeout);
void epoll_file_closed(file* epoller, file* client);

#endif

#endif /* !_OSV_POLL_H_ */
