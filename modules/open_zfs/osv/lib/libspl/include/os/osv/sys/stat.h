// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv sys/stat.h for OpenZFS userspace libraries.
 * Provides stat64 compatibility and fstat64_blk helper.
 */

#ifndef _OSV_SYS_STAT_H
#define	_OSV_SYS_STAT_H

#include_next <sys/stat.h>

/* OSv uses plain stat (64-bit by default on x86_64) */
#ifndef stat64
#define	stat64		stat
#endif
#ifndef fstat64
#define	fstat64		fstat
#endif
#ifndef lstat64
#define	lstat64		lstat
#endif

/*
 * fstat64_blk: On OSv, fstat() on a block device returns the correct
 * size in st_size (VirtIO block driver sets it at open time).
 */
static inline int
fstat64_blk(int fd, struct stat *st)
{
	return (fstat(fd, st));
}

#endif /* _OSV_SYS_STAT_H */
