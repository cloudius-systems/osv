// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_SYSMACROS_H
#define	_SPL_OSV_SYSMACROS_H

#include <sys/param.h>
#include <sys/types.h>

#ifndef MIN
#define	MIN(a, b)	((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define	MAX(a, b)	((a) < (b) ? (b) : (a))
#endif
#ifndef ABS
#define	ABS(a)		((a) < 0 ? -(a) : (a))
#endif
#ifndef SIGNOF
#define	SIGNOF(a)	((a) < 0 ? -1 : (a) > 0)
#endif
#ifndef ARRAY_SIZE
#define	ARRAY_SIZE(a)	(sizeof (a) / sizeof (a[0]))
#endif
#ifndef DIV_ROUND_UP
#define	DIV_ROUND_UP(n, d)	(((n) + (d) - 1) / (d))
#endif

/* Disk block / byte conversions */
#define	dtob(DD)	((DD) << DEV_BSHIFT)
#define	btod(BB)	(((BB) + DEV_BSIZE - 1) >> DEV_BSHIFT)
#define	btodt(BB)	((BB) >> DEV_BSHIFT)
#define	lbtod(BB)	(((offset_t)(BB) + DEV_BSIZE - 1) >> DEV_BSHIFT)

/* Page/byte conversions */
#ifndef ptob
#define	ptob(pages)	((uint64_t)(pages) << PAGE_SHIFT)
#endif
#ifndef btop
#define	btop(bytes)	((bytes) >> PAGE_SHIFT)
#endif

/* CPU count */
/* mp_ncpus is provided by netport.h as a macro to smp_processors (unsigned) */
#ifndef mp_ncpus
extern int mp_ncpus;
#endif
#define	boot_ncpus	mp_ncpus

/* Preemption */
#define	kpreempt_disable()	do { } while (0)
#define	kpreempt_enable()	do { } while (0)

/* Not a labeled system */
#define	is_system_labeled()	0

/* BCD conversions - not commonly used but defined */
#define	BYTE_TO_BCD(x)	(((x) / 10) << 4 | ((x) % 10))
#define	BCD_TO_BYTE(x)	(((x) >> 4) * 10 + ((x) & 0xf))

/* Device number macros */
#define	O_BITSMAJOR	7
#define	O_BITSMINOR	8
#define	O_MAXMAJ	0x7f
#define	O_MAXMIN	0xff

#ifdef _LP64
#define	L_BITSMAJOR	32
#define	L_BITSMINOR	32
#define	L_MAXMAJ	0xfffffffful
#define	L_MAXMIN	0xfffffffful
#else
#define	L_BITSMAJOR	14
#define	L_BITSMINOR	18
#define	L_MAXMAJ	0x3fff
#define	L_MAXMIN	0x3ffff
#endif

/* Power-of-2 macros */
#ifndef IS_P2ALIGNED
#define	IS_P2ALIGNED(v, a)	((((uintptr_t)(v)) & ((uintptr_t)(a) - 1)) == 0)
#endif
#ifndef P2ALIGN
#define	P2ALIGN(x, align)	((x) & -(align))
#endif
#ifndef P2PHASE
#define	P2PHASE(x, align)	((x) & ((align) - 1))
#endif
#ifndef P2NPHASE
#define	P2NPHASE(x, align)	(-(x) & ((align) - 1))
#endif
#ifndef P2ROUNDUP
#define	P2ROUNDUP(x, align)	(-(-(x) & -(align)))
#endif
#ifndef P2CROSS
#define	P2CROSS(x, y, align)	(((x) ^ (y)) > (align) - 1)
#endif
#ifndef P2SAMEHIGHBIT
#define	P2SAMEHIGHBIT(x, y)	(((x) ^ (y)) < ((x) & (y)))
#endif
#ifndef ISP2
#define	ISP2(x)		(((x) & ((x) - 1)) == 0)
#endif
#ifndef P2ALIGN_TYPED
#define	P2ALIGN_TYPED(x, align, type)	\
	((type)(x) & -(type)(align))
#endif
#ifndef P2ROUNDUP_TYPED
#define	P2ROUNDUP_TYPED(x, align, type)	\
	(-(-(type)(x) & -(type)(align)))
#endif
#ifndef P2PHASEUP
#define	P2PHASEUP(x, align, phase)	\
	((phase) - (((phase) - (x)) & -((uint64_t)(align))))
#endif

/* highbit/lowbit */
#ifndef highbit64
#define	highbit64(x)	((x) == 0 ? 0 : (64 - __builtin_clzll(x)))
#endif

#endif /* _SPL_OSV_SYSMACROS_H */
