/*-
 * Copyright (c) 1982, 1986, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
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
 *	@(#)kern_subr.c	8.3 (Berkeley) 1/21/94
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <osv/uio.h>

int
uiomove(void *cp, int n, struct uio *uio)
{
	assert(uio->uio_rw == UIO_READ || uio->uio_rw == UIO_WRITE);

	while (n > 0 && uio->uio_resid) {
		struct iovec *iov = uio->uio_iov;
		size_t cnt = iov->iov_len;
		if (cnt == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		if (cnt > n)
			cnt = n;

		if (uio->uio_rw == UIO_READ)
			memcpy(iov->iov_base, cp, cnt);
		else
			memcpy(cp, iov->iov_base, cnt);

		iov->iov_base = (char *)iov->iov_base + cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_offset += cnt;
		cp = (char *)cp + cnt;
		n -= cnt;
	}

	return 0;
}

/*
 * Wrapper for uiomove() that validates the arguments against a known-good
 * kernel buffer.  Currently, uiomove accepts a signed (n) argument, which
 * is almost definitely a bad thing, so we catch that here as well.  We
 * return a runtime failure, but it might be desirable to generate a runtime
 * assertion failure instead.
 */
int
uiomove_frombuf(void *buf, int buflen, struct uio *uio)
{
	size_t offset, n;

	if (uio->uio_offset < 0 || uio->uio_resid < 0 ||
	    (offset = uio->uio_offset) != uio->uio_offset)
		return (EINVAL);
	if (buflen <= 0 || offset >= buflen)
		return (0);
	if ((n = buflen - offset) > IOSIZE_MAX)
		return (EINVAL);
	return (uiomove((char *)buf + offset, n, uio));
}

/*
 * In OSv we don't really copy the iovect in because it lives in the same
 * address space.  But keeping this abstraction still allows keeping the
 * checks in one place and eases porting code.
 */
int
copyinuio(struct iovec *iovp, u_int iovcnt, struct uio **uiop)
{
	struct uio *uio;
	int i;

	*uiop = NULL;
	if (iovcnt > UIO_MAXIOV)
		return (EINVAL);
	uio = malloc(sizeof(*uio));

	uio->uio_iov = iovp;
	uio->uio_iovcnt = iovcnt;
	uio->uio_offset = -1;
	uio->uio_resid = 0;
	for (i = 0; i < iovcnt; i++) {
		if (iovp->iov_len > IOSIZE_MAX - uio->uio_resid) {
			free(uio);
			return (EINVAL);
		}
		uio->uio_resid += iovp->iov_len;
		iovp++;
	}
	*uiop = uio;
	return (0);
}
