// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv sys/mount.h stub for OpenZFS userspace libraries.
 */

#ifndef _OSV_SYS_MOUNT_H
#define	_OSV_SYS_MOUNT_H

/*
 * OSv has its own sys/mount.h through the libc.  Include it.
 */
#include_next <sys/mount.h>

/*
 * Include statfs definitions early.  OSv's sys/statfs.h defines:
 *   struct statfs  (already 64-bit on x86_64)
 *   #define statfs64  statfs    (so "struct statfs64" -> "struct statfs")
 *   #define fstatfs64 fstatfs
 * This means any code using "struct statfs64" or "statfs64()" compiles as-is.
 */
#include <sys/statfs.h>

#ifndef MS_RDONLY
#define	MS_RDONLY	1
#endif
#ifndef MS_REMOUNT
#define	MS_REMOUNT	32
#endif
#ifndef MS_BIND
#define	MS_BIND		4096
#endif
#ifndef MS_FORCE
#define	MS_FORCE	1
#endif
#ifndef MS_DETACH
#define	MS_DETACH	2
#endif

/*
 * Overlay mount is default in Linux/OSv, but for Solaris/ZFS compatibility,
 * MS_OVERLAY is defined to explicitly allow mounting over a non-empty directory.
 */
#ifndef MS_OVERLAY
#define	MS_OVERLAY	0x00000004
#endif

/*
 * MS_CRYPT indicates that encryption keys should be loaded if not already
 * available. This is a ZFS-specific flag not seen by the kernel.
 */
#ifndef MS_CRYPT
#define	MS_CRYPT	0x00000008
#endif

/* BLKFLSBUF ioctl - not supported on OSv, but referenced */
#ifndef BLKFLSBUF
#define	BLKFLSBUF	_IO(0x12, 97)
#endif

#endif /* _OSV_SYS_MOUNT_H */
