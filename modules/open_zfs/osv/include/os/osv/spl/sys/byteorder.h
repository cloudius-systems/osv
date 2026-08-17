// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_BYTEORDER_H
#define	_SPL_OSV_BYTEORDER_H

#include_next <sys/byteorder.h>

/*
 * Ensure ZFS endianness macros are defined.
 * x86_64 (OSv's only target) is always little-endian.
 */
#if !defined(_ZFS_LITTLE_ENDIAN) && !defined(_ZFS_BIG_ENDIAN)
#if defined(__x86_64__) || defined(__i386__) || \
    __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define	_ZFS_LITTLE_ENDIAN
#else
#define	_ZFS_BIG_ENDIAN
#endif
#endif

#endif /* _SPL_OSV_BYTEORDER_H */
