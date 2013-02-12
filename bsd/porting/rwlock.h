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

#include <bsd/porting/netport.h>
#include <osv/mutex.h>

struct rwlock {
    mutex_t _osv_mtx;
};

#define LOCK_FILE   __FILE__
#define LOCK_LINE   __LINE__

/*
 * Function prototypes.  Routines that start with _ are not part of the
 * external API and should not be called directly.  Wrapper macros should
 * be used instead.
 */

#define	rw_init(rw, name)	rw_init_flags((rw), (name), 0)
void	rw_init_flags(struct rwlock *rw, const char *name, int opts);
void	rw_destroy(struct rwlock *rw);
void	rw_sysinit(void *arg);
void	rw_sysinit_flags(void *arg);
int	rw_wowned(struct rwlock *rw);
void	_rw_wlock(struct rwlock *rw, const char *file, int line);
int	_rw_try_wlock(struct rwlock *rw, const char *file, int line);
void	_rw_wunlock(struct rwlock *rw, const char *file, int line);
void	_rw_rlock(struct rwlock *rw, const char *file, int line);
int	_rw_try_rlock(struct rwlock *rw, const char *file, int line);
void	_rw_runlock(struct rwlock *rw, const char *file, int line);
void	_rw_wlock_hard(struct rwlock *rw, uintptr_t tid, const char *file,
	    int line);
void	_rw_wunlock_hard(struct rwlock *rw, uintptr_t tid, const char *file,
	    int line);
int	_rw_try_upgrade(struct rwlock *rw, const char *file, int line);
void	_rw_downgrade(struct rwlock *rw, const char *file, int line);

/*
 * Public interface for lock operations.
 */

#define	rw_wlock(rw)		_rw_wlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_wunlock(rw)		_rw_wunlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_rlock(rw)		_rw_rlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_runlock(rw)		_rw_runlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_try_rlock(rw)	_rw_try_rlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_try_upgrade(rw)	_rw_try_upgrade((rw), LOCK_FILE, LOCK_LINE)
#define	rw_try_wlock(rw)	_rw_try_wlock((rw), LOCK_FILE, LOCK_LINE)
#define	rw_downgrade(rw)	_rw_downgrade((rw), LOCK_FILE, LOCK_LINE)
#define	rw_unlock(rw)	do {						\
	if (rw_wowned(rw))						\
		rw_wunlock(rw);						\
	else								\
		rw_runlock(rw);						\
} while (0)

struct rw_args {
	struct rwlock	*ra_rw;
	const char 	*ra_desc;
};

struct rw_args_flags {
	struct rwlock	*ra_rw;
	const char 	*ra_desc;
	int		ra_flags;
};

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

#endif /* !_SYS_RWLOCK_H_ */
