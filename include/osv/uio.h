/*-
 * Copyright (c) 1982, 1986, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)uio.h	8.5 (Berkeley) 2/22/94
 * $FreeBSD$
 */

#ifndef _UIO_H_
#define	_UIO_H_

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <limits.h>

__BEGIN_DECLS

enum	uio_rw { UIO_READ, UIO_WRITE };

/*
 * Safe default to prevent possible overflows in user code, otherwise could
 * be SSIZE_T_MAX.
 */
#define IOSIZE_MAX      INT_MAX

#define UIO_MAXIOV 1024

#define UIO_SYSSPACE 0

struct uio {
	struct iovec *uio_iov;		/* scatter/gather list */
	int	uio_iovcnt;		/* length of scatter/gather list */
	off_t	uio_offset;		/* offset in target object */
	ssize_t	uio_resid;		/* remaining bytes to process */
	enum	uio_rw uio_rw;		/* operation */
};

int	copyinuio(struct iovec *iovp, u_int iovcnt, struct uio **uiop);
int	uiomove(void *cp, int n, struct uio *uio);

__END_DECLS

#endif /* !_UIO_H_ */
