// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv sys/sysmacros.h for OpenZFS userspace libraries.
 * Provides P2*_TYPED, IS_P2ALIGNED, ARRAY_SIZE and related macros.
 */

#ifndef _OSV_SYS_SYSMACROS_H
#define	_OSV_SYS_SYSMACROS_H

#include_next <sys/sysmacros.h>

/* common macros */
#ifndef MIN
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)	((a) < (b) ? (b) : (a))
#endif
#ifndef ABS
#define	ABS(a)		((a) < 0 ? -(a) : (a))
#endif
#ifndef ARRAY_SIZE
#define	ARRAY_SIZE(a)	(sizeof (a) / sizeof (a[0]))
#endif
#ifndef DIV_ROUND_UP
#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#endif

#define	makedevice(maj, min)	makedev(maj, min)
#define	_sysconf(a)		sysconf(a)

/*
 * Compatibility macros/typedefs needed for Solaris -> OSv port
 */
#define	P2CROSS(x, y, align)	(((x) ^ (y)) > (align) - 1)
#define	P2ROUNDUP(x, align)	((((x) - 1) | ((align) - 1)) + 1)
#define	P2BOUNDARY(off, len, align) \
				(((off) ^ ((off) + (len) - 1)) > (align) - 1)
#define	P2PHASE(x, align)	((x) & ((align) - 1))
#define	P2NPHASE(x, align)	(-(x) & ((align) - 1))
#define	P2NPHASE_TYPED(x, align, type) \
				(-(type)(x) & ((type)(align) - 1))
#define	ISP2(x)			(((x) & ((x) - 1)) == 0)
#define	IS_P2ALIGNED(v, a)	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)

/*
 * Typed versions of P2 macros.
 */
#define	P2ALIGN_TYPED(x, align, type)		\
	((type)(x) & -(type)(align))
#define	P2PHASE_TYPED(x, align, type)		\
	((type)(x) & ((type)(align) - 1))
#define	P2NPHASE_TYPED(x, align, type)		\
	(-(type)(x) & ((type)(align) - 1))
#define	P2ROUNDUP_TYPED(x, align, type)		\
	((((type)(x) - 1) | ((type)(align) - 1)) + 1)
#define	P2END_TYPED(x, align, type)		\
	(-(~(type)(x) & -(type)(align)))
#define	P2PHASEUP_TYPED(x, align, phase, type)	\
	((type)(phase) - (((type)(phase) - (type)(x)) & -(type)(align)))
#define	P2CROSS_TYPED(x, y, align, type)	\
	(((type)(x) ^ (type)(y)) > (type)(align) - 1)
#define	P2SAMEHIGHBIT_TYPED(x, y, type)		\
	(((type)(x) ^ (type)(y)) < ((type)(x) & (type)(y)))

/* avoid any possibility of clashing with <stddef.h> version */
#if defined(_KERNEL) && !defined(_KMEMUSER) && !defined(offsetof)
#define	offsetof(s, m)	((size_t)(&(((s *)0)->m)))
#endif

#endif /* _OSV_SYS_SYSMACROS_H */
