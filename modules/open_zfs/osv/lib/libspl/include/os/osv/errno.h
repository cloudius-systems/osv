// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv errno.h wrapper for OpenZFS userspace libraries.
 *
 * Wraps the standard <errno.h> and adds OpenZFS-specific errno aliases
 * (ECKSUM, EFRAGS, ENOTACTIVE) that are not part of POSIX/Linux but are
 * used throughout the OpenZFS userspace code.
 *
 * This file is found first (via -isystem .../libspl/include/os/osv) when
 * userspace ZFS code includes <errno.h>, so we can augment the standard set.
 */

#ifndef _LIBSPL_OSV_ERRNO_H
#define	_LIBSPL_OSV_ERRNO_H

#include_next <errno.h>

/*
 * Solaris-specific errnos used by OpenZFS that are not in POSIX.
 * Musl defines EBADE=52, EBADR=53, ENOANO=55 in bits/errno.h.
 */
#ifndef ECKSUM
#define	ECKSUM		EBADE		/* ZFS checksum error */
#endif
#ifndef EFRAGS
#define	EFRAGS		EBADR		/* ZFS fragmentation error */
#endif
#ifndef ENOTACTIVE
#define	ENOTACTIVE	ENOANO		/* pool/vdev not active */
#endif

#endif /* _LIBSPL_OSV_ERRNO_H */
