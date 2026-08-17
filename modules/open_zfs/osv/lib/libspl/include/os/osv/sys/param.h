// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv sys/param.h for OpenZFS userspace libraries.
 */

#ifndef _OSV_SYS_PARAM_H
#define	_OSV_SYS_PARAM_H

#include_next <sys/param.h>
#include <unistd.h>

/*
 * File system parameters and macros.
 */
#ifndef MAXBSIZE
#define	MAXBSIZE	8192
#endif
#ifndef DEV_BSIZE
#define	DEV_BSIZE	512
#endif
#ifndef DEV_BSHIFT
#define	DEV_BSHIFT	9		/* log2(DEV_BSIZE) */
#endif

#ifndef MAXPATHLEN
#define	MAXPATHLEN	4096
#endif

#ifndef MAXNAMELEN
#define	MAXNAMELEN	256
#endif

#ifndef MAXHOSTNAMELEN
#define	MAXHOSTNAMELEN	256
#endif

#ifndef MAXOFFSET_T
#define	MAXOFFSET_T	LLONG_MAX
#endif

#ifndef UID_NOBODY
#define	UID_NOBODY	60001
#define	GID_NOBODY	UID_NOBODY
#define	UID_NOACCESS	60002
#endif

#ifndef MAXUID
#define	MAXUID		UINT32_MAX
#define	MAXPROJID	MAXUID
#endif

#ifdef	PAGESIZE
#undef	PAGESIZE
#endif

extern size_t spl_pagesize(void);
#define	PAGESIZE	(spl_pagesize())

#endif /* _OSV_SYS_PARAM_H */
