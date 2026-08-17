// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL misc - miscellaneous definitions needed by OpenZFS.
 * Note: the old compat misc.h is blocked (_OPENSOLARIS_SYS_MISC_H_)
 * to avoid the `extern struct utsname utsname` conflict.
 * We provide everything OpenZFS needs here.
 */
#ifndef _SPL_OSV_MISC_H
#define	_SPL_OSV_MISC_H

#include <errno.h>
#include <sys/limits.h>

/*
 * MAXUID from the compat misc.h.
 */
#ifndef MAXUID
#define	MAXUID	UID_MAX
#endif

/*
 * ACL constants.
 */
#ifndef _ACL_ACLENT_ENABLED
#define	_ACL_ACLENT_ENABLED	0x1
#endif
#ifndef _ACL_ACE_ENABLED
#define	_ACL_ACE_ENABLED	0x2
#endif

/*
 * Solaris/ZFS-specific error codes not in POSIX errno.h.
 */
#ifndef ECKSUM
#define	ECKSUM		97
#endif
#ifndef ENOTACTIVE
#define	ENOTACTIVE	98
#endif
#ifndef EFRAGS
#define	EFRAGS		99
#endif

/*
 * noinline attribute for GCC.
 */
#ifndef noinline
#define	noinline	__attribute__((noinline))
#endif

/*
 * PAGESHIFT alias (some code uses PAGESHIFT instead of PAGE_SHIFT).
 */
#ifndef PAGESHIFT
#define	PAGESHIFT	PAGE_SHIFT
#endif

/*
 * struct opensolaris_utsname - system identification.
 * Used by utsname_t typedef in zfs_context_os.h.
 */
struct opensolaris_utsname {
	const char	*sysname;
	const char	*nodename;
	const char	*release;
	char		version[32];
	const char	*machine;
};

/* SPEC_MAXOFFSET_T is defined by OpenZFS's sys/zvol.h */

#endif /* _SPL_OSV_MISC_H */
