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

#ifndef _OPENSOLARIS_SYS_CONDVAR_H_
#define	_OPENSOLARIS_SYS_CONDVAR_H_

#include <osv/condvar.h>

typedef struct condvar	kcondvar_t;

typedef enum {
	CV_DEFAULT,
	CV_DRIVER
} kcv_type_t;

#define cv_init(cv, name, type, arg)	memset(cv, 0, sizeof(*cv))
#define cv_destroy(cv)			do { } while(0)
#define cv_signal(cv)			condvar_wake_one(cv)
#define cv_broadcast(cv)		condvar_wake_all(cv)
#define cv_wait(cv, mutex)		condvar_wait(cv, mutex, 0)
#define cv_timedwait(cv, mutex, tmo)	condvar_wait(cv, mutex, tmo)

#endif	/* _OPENSOLARIS_SYS_CONDVAR_H_ */
