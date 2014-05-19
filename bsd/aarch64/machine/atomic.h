/*-
 * Copyright (c) 1998 Doug Rabson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
#ifndef _MACHINE_ATOMIC_H_
#define	_MACHINE_ATOMIC_H_

#if 0
#ifndef _SYS_CDEFS_H_
#error this file needs sys/cdefs.h as a prerequisite
#endif
#endif

#define	mb()	__asm __volatile("dmb ISH;" : : : "memory")
#define	wmb()	__asm __volatile("dmb ISHST;" : : : "memory")
#define	rmb()	__asm __volatile("dmb ISHLD;" : : : "memory")

/*
 * Various simple operations on memory, each of which is atomic in the
 * presence of interrupts and multiple processors.
 *
 * atomic_set_char(P, V)	(*(u_char *)(P) |= (V))
 * atomic_clear_char(P, V)	(*(u_char *)(P) &= ~(V))
 * atomic_add_char(P, V)	(*(u_char *)(P) += (V))
 * atomic_subtract_char(P, V)	(*(u_char *)(P) -= (V))
 *
 * atomic_set_short(P, V)	(*(u_short *)(P) |= (V))
 * atomic_clear_short(P, V)	(*(u_short *)(P) &= ~(V))
 * atomic_add_short(P, V)	(*(u_short *)(P) += (V))
 * atomic_subtract_short(P, V)	(*(u_short *)(P) -= (V))
 *
 * atomic_set_int(P, V)		(*(u_int *)(P) |= (V))
 * atomic_clear_int(P, V)	(*(u_int *)(P) &= ~(V))
 * atomic_add_int(P, V)		(*(u_int *)(P) += (V))
 * atomic_subtract_int(P, V)	(*(u_int *)(P) -= (V))
 * atomic_readandclear_int(P)	(return (*(u_int *)(P)); *(u_int *)(P) = 0;)
 *
 * atomic_set_long(P, V)	(*(u_long *)(P) |= (V))
 * atomic_clear_long(P, V)	(*(u_long *)(P) &= ~(V))
 * atomic_add_long(P, V)	(*(u_long *)(P) += (V))
 * atomic_subtract_long(P, V)	(*(u_long *)(P) -= (V))
 * atomic_readandclear_long(P)	(return (*(u_long *)(P)); *(u_long *)(P) = 0;)
 */

/*
 * The above functions are expanded inline in the statically-linked
 * kernel.  Lock prefixes are generated if an SMP kernel is being
 * built.
 *
 * Kernel modules call real functions which are built into the kernel.
 * This allows kernel modules to be portable between UP and SMP systems.
 */
#define __GNUCLIKE_ASM
#define	ATOMIC_ASM(NAME, TYPE, OP, CONS, V)			\
void atomic_##NAME##_##TYPE(volatile u_##TYPE *p, u_##TYPE v);	\
void atomic_##NAME##_barr_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

int	atomic_cmpset_int(volatile u_int *dst, u_int expect, u_int src);
int	atomic_cmpset_long(volatile u_long *dst, u_long expect, u_long src);

static __inline u_int atomic_fetchadd_int(volatile u_int *p, u_int val)
{
    u_int result;
    u_int status;
    __asm __volatile("1: ldaxr %w0, %1 ; "
                     "   add   %w2, %w2, %w0 ; "
                     "   stlxr %w3, %w2, %1 ; "
                     "   cbnz  %w3, 1b ; "
                     : "=&r"(result), "+Q"(*p), "+r"(val), "=&r"(status));

    return result;
}

static __inline u_long atomic_fetchadd_long(volatile u_long *p, u_long val)
{
    u_long result;
    u_int status;
    __asm __volatile("1: ldaxr %0, %1 ; "
                     "   add   %2, %2, %0 ; "
                     "   stlxr %w3, %2, %1 ; "
                     "   cbnz  %w3, 1b ; "
                     : "=&r"(result), "+Q"(*p), "+r"(val), "=&r"(status));

    return result;
}

static __inline void atomic_store_rel_int(volatile u_int *p, u_int val)
{
    __asm __volatile("stlr %w1, %0 ; " : "+Q"(*p) : "r"(val));
}

static __inline void atomic_store_rel_long(volatile u_long *p, u_long val)
{
    __asm __volatile("stlr %1, %0 ; " : "+Q"(*p) : "r"(val));
}

static __inline void atomic_add_int(volatile u_int *p, u_int val)
{
    (void)atomic_fetchadd_int(p, val);
}

static __inline void atomic_add_barr_int(volatile u_int *p, u_int val)
{
    (void)atomic_fetchadd_int(p, val);
}

static __inline void atomic_add_long(volatile u_long *p, u_long val)
{
    (void)atomic_fetchadd_long(p, val);
}

static __inline void atomic_add_barr_long(volatile u_long *p, u_long val)
{
    (void)atomic_fetchadd_long(p, val);
}

static __inline void atomic_subtract_int(volatile u_int *p, u_int val)
{
    (void)atomic_fetchadd_int(p, -val);
}

static __inline void atomic_subtract_long(volatile u_long *p, u_long val)
{
    (void)atomic_fetchadd_long(p, -val);
}

#define	ATOMIC_LOAD(TYPE, LOP)					\
u_##TYPE	atomic_load_acq_##TYPE(volatile u_##TYPE *p)
#define	ATOMIC_STORE(TYPE)					\
void		atomic_store_rel_##TYPE(volatile u_##TYPE *p, u_##TYPE v)

ATOMIC_ASM(set,	     char,  "unused", "unused",  v);
ATOMIC_ASM(clear,    char,  "unused", "unused", ~v);
ATOMIC_ASM(add,	     char,  "unused", "unused", v);
ATOMIC_ASM(subtract, char,  "unused", "unused", v);

ATOMIC_ASM(set,	     short, "unused", "unused", v);
ATOMIC_ASM(clear,    short, "unused", "unused", ~v);
ATOMIC_ASM(add,	     short, "unused", "unused",  v);
ATOMIC_ASM(subtract, short, "unused", "unused",  v);

ATOMIC_ASM(set,	     int,   "unused", "unused",  v);
ATOMIC_ASM(clear,    int,   "unused", "unused", ~v);
ATOMIC_ASM(add,	     int,   "unused", "unused",  v);
ATOMIC_ASM(subtract, int,   "unused", "unused",  v);

ATOMIC_ASM(set,	     long,  "unused", "unused",  v);
ATOMIC_ASM(clear,    long,  "unused", "unused", ~v);
ATOMIC_ASM(add,	     long,  "unused", "unused",  v);
ATOMIC_ASM(subtract, long,  "unused", "unused",  v);

ATOMIC_LOAD(char,  "unused");
ATOMIC_LOAD(short, "unused");
ATOMIC_LOAD(int,   "unused");
ATOMIC_LOAD(long,  "unused");

ATOMIC_STORE(char);
ATOMIC_STORE(short);
ATOMIC_STORE(int);
ATOMIC_STORE(long);

#undef ATOMIC_ASM
#undef ATOMIC_LOAD
#undef ATOMIC_STORE

#ifndef WANT_FUNCTIONS

/* Read the current value and store a zero in the destination. */
u_int	atomic_readandclear_int(volatile u_int *addr);
u_long	atomic_readandclear_long(volatile u_long *addr);

#define	atomic_set_acq_char		atomic_set_barr_char
#define	atomic_set_rel_char		atomic_set_barr_char
#define	atomic_clear_acq_char		atomic_clear_barr_char
#define	atomic_clear_rel_char		atomic_clear_barr_char
#define	atomic_add_acq_char		atomic_add_barr_char
#define	atomic_add_rel_char		atomic_add_barr_char
#define	atomic_subtract_acq_char	atomic_subtract_barr_char
#define	atomic_subtract_rel_char	atomic_subtract_barr_char

#define	atomic_set_acq_short		atomic_set_barr_short
#define	atomic_set_rel_short		atomic_set_barr_short
#define	atomic_clear_acq_short		atomic_clear_barr_short
#define	atomic_clear_rel_short		atomic_clear_barr_short
#define	atomic_add_acq_short		atomic_add_barr_short
#define	atomic_add_rel_short		atomic_add_barr_short
#define	atomic_subtract_acq_short	atomic_subtract_barr_short
#define	atomic_subtract_rel_short	atomic_subtract_barr_short

#define	atomic_set_acq_int		atomic_set_barr_int
#define	atomic_set_rel_int		atomic_set_barr_int
#define	atomic_clear_acq_int		atomic_clear_barr_int
#define	atomic_clear_rel_int		atomic_clear_barr_int
#define	atomic_add_acq_int		atomic_add_barr_int
#define	atomic_add_rel_int		atomic_add_barr_int
#define	atomic_subtract_acq_int		atomic_subtract_barr_int
#define	atomic_subtract_rel_int		atomic_subtract_barr_int
#define	atomic_cmpset_acq_int		atomic_cmpset_int
#define	atomic_cmpset_rel_int		atomic_cmpset_int

#define	atomic_set_acq_long		atomic_set_barr_long
#define	atomic_set_rel_long		atomic_set_barr_long
#define	atomic_clear_acq_long		atomic_clear_barr_long
#define	atomic_clear_rel_long		atomic_clear_barr_long
#define	atomic_add_acq_long		atomic_add_barr_long
#define	atomic_add_rel_long		atomic_add_barr_long
#define	atomic_subtract_acq_long	atomic_subtract_barr_long
#define	atomic_subtract_rel_long	atomic_subtract_barr_long
#define	atomic_cmpset_acq_long		atomic_cmpset_long
#define	atomic_cmpset_rel_long		atomic_cmpset_long

/* Operations on 8-bit bytes. */
#define	atomic_set_8		atomic_set_char
#define	atomic_set_acq_8	atomic_set_acq_char
#define	atomic_set_rel_8	atomic_set_rel_char
#define	atomic_clear_8		atomic_clear_char
#define	atomic_clear_acq_8	atomic_clear_acq_char
#define	atomic_clear_rel_8	atomic_clear_rel_char
#define	atomic_add_8		atomic_add_char
#define	atomic_add_acq_8	atomic_add_acq_char
#define	atomic_add_rel_8	atomic_add_rel_char
#define	atomic_subtract_8	atomic_subtract_char
#define	atomic_subtract_acq_8	atomic_subtract_acq_char
#define	atomic_subtract_rel_8	atomic_subtract_rel_char
#define	atomic_load_acq_8	atomic_load_acq_char
#define	atomic_store_rel_8	atomic_store_rel_char

/* Operations on 16-bit words. */
#define	atomic_set_16		atomic_set_short
#define	atomic_set_acq_16	atomic_set_acq_short
#define	atomic_set_rel_16	atomic_set_rel_short
#define	atomic_clear_16		atomic_clear_short
#define	atomic_clear_acq_16	atomic_clear_acq_short
#define	atomic_clear_rel_16	atomic_clear_rel_short
#define	atomic_add_16		atomic_add_short
#define	atomic_add_acq_16	atomic_add_acq_short
#define	atomic_add_rel_16	atomic_add_rel_short
#define	atomic_subtract_16	atomic_subtract_short
#define	atomic_subtract_acq_16	atomic_subtract_acq_short
#define	atomic_subtract_rel_16	atomic_subtract_rel_short
#define	atomic_load_acq_16	atomic_load_acq_short
#define	atomic_store_rel_16	atomic_store_rel_short

/* Operations on 32-bit double words. */
#define	atomic_set_32		atomic_set_int
#define	atomic_set_acq_32	atomic_set_acq_int
#define	atomic_set_rel_32	atomic_set_rel_int
#define	atomic_clear_32		atomic_clear_int
#define	atomic_clear_acq_32	atomic_clear_acq_int
#define	atomic_clear_rel_32	atomic_clear_rel_int
#define	atomic_add_32		atomic_add_int
#define	atomic_add_acq_32	atomic_add_acq_int
#define	atomic_add_rel_32	atomic_add_rel_int
#define	atomic_subtract_32	atomic_subtract_int
#define	atomic_subtract_acq_32	atomic_subtract_acq_int
#define	atomic_subtract_rel_32	atomic_subtract_rel_int
#define	atomic_load_acq_32	atomic_load_acq_int
#define	atomic_store_rel_32	atomic_store_rel_int
#define	atomic_cmpset_32	atomic_cmpset_int
#define	atomic_cmpset_acq_32	atomic_cmpset_acq_int
#define	atomic_cmpset_rel_32	atomic_cmpset_rel_int
#define	atomic_readandclear_32	atomic_readandclear_int
#define	atomic_fetchadd_32	atomic_fetchadd_int

/* Operations on 64-bit quad words. */
#define	atomic_set_64		atomic_set_long
#define	atomic_set_acq_64	atomic_set_acq_long
#define	atomic_set_rel_64	atomic_set_rel_long
#define	atomic_clear_64		atomic_clear_long
#define	atomic_clear_acq_64	atomic_clear_acq_long
#define	atomic_clear_rel_64	atomic_clear_rel_long
#define	atomic_add_64		atomic_add_long
#define	atomic_add_acq_64	atomic_add_acq_long
#define	atomic_add_rel_64	atomic_add_rel_long
#define	atomic_subtract_64	atomic_subtract_long
#define	atomic_subtract_acq_64	atomic_subtract_acq_long
#define	atomic_subtract_rel_64	atomic_subtract_rel_long
#define	atomic_load_acq_64	atomic_load_acq_long
#define	atomic_store_rel_64	atomic_store_rel_long
#define	atomic_cmpset_64	atomic_cmpset_long
#define	atomic_cmpset_acq_64	atomic_cmpset_acq_long
#define	atomic_cmpset_rel_64	atomic_cmpset_rel_long
#define	atomic_readandclear_64	atomic_readandclear_long

/* Operations on pointers. */
#define	atomic_set_ptr		atomic_set_long
#define	atomic_set_acq_ptr	atomic_set_acq_long
#define	atomic_set_rel_ptr	atomic_set_rel_long
#define	atomic_clear_ptr	atomic_clear_long
#define	atomic_clear_acq_ptr	atomic_clear_acq_long
#define	atomic_clear_rel_ptr	atomic_clear_rel_long
#define	atomic_add_ptr		atomic_add_long
#define	atomic_add_acq_ptr	atomic_add_acq_long
#define	atomic_add_rel_ptr	atomic_add_rel_long
#define	atomic_subtract_ptr	atomic_subtract_long
#define	atomic_subtract_acq_ptr	atomic_subtract_acq_long
#define	atomic_subtract_rel_ptr	atomic_subtract_rel_long
#define	atomic_load_acq_ptr	atomic_load_acq_long
#define	atomic_store_rel_ptr	atomic_store_rel_long
#define	atomic_cmpset_ptr	atomic_cmpset_long
#define	atomic_cmpset_acq_ptr	atomic_cmpset_acq_long
#define	atomic_cmpset_rel_ptr	atomic_cmpset_rel_long
#define	atomic_readandclear_ptr	atomic_readandclear_long

#endif /* !WANT_FUNCTIONS */

#endif /* !_MACHINE_ATOMIC_H_ */
