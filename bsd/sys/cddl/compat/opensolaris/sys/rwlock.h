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

#ifndef _OPENSOLARIS_SYS_RWLOCK_H_
#define	_OPENSOLARIS_SYS_RWLOCK_H_

#include <osv/mutex.h>

// get rid of the conflicting freebsd defintions
#undef rw_init
#undef rw_downgrade

typedef enum {
	RW_DEFAULT = 4		/* kernel default rwlock */
} krw_type_t;

typedef enum {
	RW_WRITER,
	RW_READER
} krw_t;

// try to get away with simply using a mutex for now


typedef	struct mutex	krwlock_t;

#define	RW_READ_HELD(x)		(mutex_owned((x)))
#define	RW_WRITE_HELD(x)	(mutex_owned((x)))
#define	RW_LOCK_HELD(x)		(mutex_owned((x)))
#define	RW_ISWRITER(x)		(1)

#define	rw_init(lock, desc, type, arg) \
	mutex_init(lock, desc, type, arg)
#define	rw_enter(lock, how) \
	mutex_lock(lock)
#define	rw_tryenter(lock, how) \
	mutex_trylock(lock)
#define	rw_exit(lock) \
	mutex_unlock(lock)
#define	rw_downgrade(lock)	do { } while (0)
#define	rw_tryupgrade(lock)	(1)
#define	rw_read_held(lock)	(mutex_owned((x)))
#define	rw_write_held(lock)	(mutex_owned((x)))
#define	rw_lock_held(lock)	(mutex_owned((x)))
#define	rw_iswriter(lock)	(1)
#define rw_destroy(lock)	do { } while(0)

#if 0
/* TODO: Change to sx_xholder() once it is moved from kern_sx.c to sx.h. */
#define	rw_owner(lock)		((lock)->sx_lock & SX_LOCK_SHARED ? NULL : (struct thread *)SX_OWNER((lock)->sx_lock))
#endif

#endif	/* _OPENSOLARIS_SYS_RWLOCK_H_ */
