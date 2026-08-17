// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL isa_defs.h - ISA definitions for OpenZFS.
 * Based on the FreeBSD version. OSv only supports x86_64.
 */
#ifndef	_SPL_OSV_ISA_DEFS_H
#define	_SPL_OSV_ISA_DEFS_H

#include <endian.h>

#ifdef	__cplusplus
extern "C" {
#endif

#if defined(__x86_64) || defined(__amd64)

#if !defined(__amd64)
#define	__amd64
#endif

#if !defined(__x86)
#define	__x86
#endif

#if !defined(_LP64)
#error "_LP64 not defined"
#endif
#define	_SUNOS_VTOC_16

#else
#error "ISA not supported - OSv only supports x86_64"
#endif

#if __BYTE_ORDER == __BIG_ENDIAN
#define	_ZFS_BIG_ENDIAN
#elif __BYTE_ORDER == __LITTLE_ENDIAN
#define	_ZFS_LITTLE_ENDIAN
#else
#error "unknown byte order"
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_OSV_ISA_DEFS_H */
