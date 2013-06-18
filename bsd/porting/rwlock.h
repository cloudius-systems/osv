/*-
 * Copyright (c) 2006 John Baldwin <jhb@FreeBSD.org>
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
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 * $FreeBSD$
 */

#ifndef _SYS_RWLOCK_H_
#define _SYS_RWLOCK_H_

/* The real implementation is here */
#include <osv/rwlock.h>

__BEGIN_DECLS

#define LO_INITIALIZED  0x00010000  /* Lock has been initialized. */
#define LO_RECURSABLE   0x00080000  /* Lock may recurse. */

/*
 * Function prototypes.  Routines that start with _ are not part of the
 * external API and should not be called directly.  Wrapper macros should
 * be used instead.
 */

#define rw_init(rw, name)   rw_init_flags((rw), (name), 0)
#define rw_destroy(rw)      rwlock_destroy((rw))
static inline
void rw_init_flags(struct rwlock *rw, const char *name, int opts) {
    rwlock_init(rw);
}

/*
 * Public interface for lock operations.
 */

#define	rw_unlock(rw)	do {						\
	if (rw_wowned(rw))						\
		rw_wunlock(rw);						\
	else								\
		rw_runlock(rw);						\
} while (0)

/* Disable static initialization */
#define	RW_SYSINIT(name, rw, desc)
#define	RW_SYSINIT_FLAGS(name, rw, desc, flags)

/*
 * Options passed to rw_init_flags().
 */
#define	RW_DUPOK	0x01
#define	RW_NOPROFILE	0x02
#define	RW_NOWITNESS	0x04
#define	RW_QUIET	0x08
#define	RW_RECURSE	0x10

#define	rw_assert(rw, what)

__END_DECLS

#endif /* !_SYS_RWLOCK_H_ */
