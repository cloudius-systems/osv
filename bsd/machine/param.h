/*-
 * Copyright (c) 1982, 1986, 1989, 1993
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
 *	@(#)param.h	8.3 (Berkeley) 4/4/95
 * $FreeBSD$
 */

#ifndef _SYS_MACHINE_PARAM_H_
#define _SYS_MACHINE_PARAM_H_

#define	BSD	199506		/* System version (year & month). */
#define BSD4_3	1
#define BSD4_4	1

/* 
 * __FreeBSD_version numbers are documented in the Porter's Handbook.
 * If you bump the version for any reason, you should update the documentation
 * there.
 * Currently this lives here in the doc/ repository:
 *
 *	head/en_US.ISO8859-1/books/porters-handbook/book.xml
 *
 * scheme is:  <major><two digit minor>Rxx
 *		'R' is in the range 0 to 4 if this is a release branch or
 *		x.0-CURRENT before RELENG_*_0 is created, otherwise 'R' is
 *		in the range 5 to 9.
 */
#undef __FreeBSD_version
#define __FreeBSD_version 901502	/* Master, propagated to newvers */

/*
 * __FreeBSD_kernel__ indicates that this system uses the kernel of FreeBSD,
 * which by definition is always true on FreeBSD. This macro is also defined
 * on other systems that use the kernel of FreeBSD, such as GNU/kFreeBSD.
 *
 * It is tempting to use this macro in userland code when we want to enable
 * kernel-specific routines, and in fact it's fine to do this in code that
 * is part of FreeBSD itself.  However, be aware that as presence of this
 * macro is still not widespread (e.g. older FreeBSD versions, 3rd party
 * compilers, etc), it is STRONGLY DISCOURAGED to check for this macro in
 * external applications without also checking for __FreeBSD__ as an
 * alternative.
 */
#undef __FreeBSD_kernel__
#define __FreeBSD_kernel__

#ifdef _KERNEL
#define	P_OSREL_SIGWAIT		700000
#define	P_OSREL_SIGSEGV		700004
#define	P_OSREL_MAP_ANON	800104
#endif

#ifndef LOCORE
#include <sys/types.h>
#endif

/*
 * Machine-independent constants (some used in following include files).
 * Redefined constants are from POSIX 1003.1 limits file.
 *
 * MAXCOMLEN should be >= sizeof(ac_comm) (see <acct.h>)
 */
#if 0
#include <bsd/sys/sys/syslimits.h>
#endif

/*
 * Constants related to network buffer management.
 * MCLBYTES must be no larger than PAGE_SIZE.
 */
#ifndef	MSIZE
#define MSIZE		256		/* size of an mbuf */
#endif	/* MSIZE */

#ifndef	MCLSHIFT
#define MCLSHIFT	11		/* convert bytes to mbuf clusters */
#endif	/* MCLSHIFT */

#define MCLBYTES	(1 << MCLSHIFT)	/* size of an mbuf cluster */

#if PAGE_SIZE < 2048
#define	MJUMPAGESIZE	MCLBYTES
#elif PAGE_SIZE <= 8192
#define	MJUMPAGESIZE	PAGE_SIZE
#else
#define	MJUMPAGESIZE	(8 * 1024)
#endif

#define	MJUM9BYTES	(9 * 1024)	/* jumbo cluster 9k */
#define	MJUM16BYTES	(16 * 1024)	/* jumbo cluster 16k */

/* Macros for min/max. */
#ifndef MIN
#define	MIN(a,b) (((a)<(b))?(a):(b))
#endif

#ifndef MAX
#define	MAX(a,b) (((a)>(b))?(a):(b))
#endif

/*
 * Scale factor for scaled integers used to count %cpu time and load avgs.
 *
 * The number of CPU `tick's that map to a unique `%age' can be expressed
 * by the formula (1 / (2 ^ (FSHIFT - 11))).  The maximum load average that
 * can be calculated (assuming 32 bits) can be closely approximated using
 * the formula (2 ^ (2 * (16 - FSHIFT))) for (FSHIFT < 15).
 *
 * For the scheduler to maintain a 1:1 mapping of CPU `tick' to `%age',
 * FSHIFT must be at least 11; this gives us a maximum load avg of ~1024.
 */
#define	FSHIFT	11		/* bits to right of fixed binary point */
#define FSCALE	(1<<FSHIFT)

#define dbtoc(db)			/* calculates devblks to pages */ \
	((db + (ctodb(1) - 1)) >> (PAGE_SHIFT - DEV_BSHIFT))
 
#define ctodb(db)			/* calculates pages to devblks */ \
	((db) << (PAGE_SHIFT - DEV_BSHIFT))

/*
 * Old spelling of __containerof().
 */
#define	member2struct(s, m, x)						\
	((struct s *)(void *)((char *)(x) - offsetof(struct s, m)))

/*
 * Access a variable length array that has been declared as a fixed
 * length array.
 */
#define __PAST_END(array, offset) (((__typeof__(*(array)) *)(array))[offset])

#endif	/* _SYS_MACHINE_PARAM_H_ */
