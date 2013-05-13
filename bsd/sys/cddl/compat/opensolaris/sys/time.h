/*-
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_TIME_H_
#define	_OPENSOLARIS_SYS_TIME_H_

#include <time.h>

#define SEC		1
#define MILLISEC	1000
#define MICROSEC	1000000
#define NANOSEC		1000000000

typedef longlong_t	hrtime_t;

#if defined(__i386__) || defined(__powerpc__)
#define	TIMESPEC_OVERFLOW(ts)						\
	((ts)->tv_sec < INT32_MIN || (ts)->tv_sec > INT32_MAX)
#else
#define	TIMESPEC_OVERFLOW(ts)						\
	((ts)->tv_sec < INT64_MIN || (ts)->tv_sec > INT64_MAX)
#endif

#if 0 //def _KERNEL
static __inline hrtime_t
gethrtime(void) {

	struct timespec ts;
	hrtime_t nsec;

	getnanouptime(&ts);
	nsec = (hrtime_t)ts.tv_sec * NANOSEC + ts.tv_nsec;
	return (nsec);
}

#define	gethrestime_sec()	(time_second)
#define	gethrestime(ts)		getnanotime(ts)
#define	gethrtime_waitfree()	gethrtime()

#else

#define CLOCK_UPTIME CLOCK_REALTIME	// XXX: hack, need better in-kernel timestamp

static __inline hrtime_t gethrtime(void) {
	struct timespec ts;
	clock_gettime(CLOCK_UPTIME,&ts);
	return (((u_int64_t) ts.tv_sec) * NANOSEC + ts.tv_nsec);
}
#define	gethrtime_waitfree()	gethrtime()

static __inline uint64_t gethrestime_sec(void) {
	struct timespec ts;
	clock_gettime(CLOCK_UPTIME,&ts);
	return ((u_int64_t) ts.tv_sec);
}

#define	gethrestime(ts)		 clock_gettime(CLOCK_REALTIME, ts)


#define	gethrestime(ts)		 clock_gettime(CLOCK_REALTIME, ts)

#endif	/* _KERNEL */

extern int nsec_per_tick;	/* nanoseconds per clock tick */

static __inline int64_t
ddi_get_lbolt64(void)
{
	return (gethrtime() / nsec_per_tick);
}

static __inline clock_t
ddi_get_lbolt(void)
{

	return (ddi_get_lbolt64());
}


#endif	/* !_OPENSOLARIS_SYS_TIME_H_ */
