/* Copyright (c) 2012-2013, Linaro Limited
   All rights reserved.

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are met:
       * Redistributions of source code must retain the above copyright
         notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above copyright
         notice, this list of conditions and the following disclaimer in the
         documentation and/or other materials provided with the distribution.
       * Neither the name of the Linaro nor the
         names of its contributors may be used to endorse or promote products
         derived from this software without specific prior written permission.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

/* Assumptions:
 *
 * ARMv8-a, AArch64
 * Unaligned accesses
 *
 */

/*
 * Modified 2015 by Claudio Fontana <claudio.fontana@huawei.com> for OSv
 *
 * This file is a merge of memset.S memcpy.S and memmove.S
 * from newlib 2.2.0-1.
 *
 * Changes:
 * - adapted interface for memcpy_backwards
 * - some preprocessor related changes due to the merge of three files
 * - always #define MAYBE_VIRT 1 since we are running virtualized.
 */

#define MAYBE_VIRT 1

#define dstin x0
#define src   x1
#define count x2
#define tmp1  x3
#define tmp1w w3
#define tmp2  x4
#define tmp2w w4
#define tmp3  x5
#define tmp3w w5
#define dst   x6

#define A_l   x7
#define A_h   x8
#define B_l   x9
#define B_h   x10
#define C_l   x11
#define C_h   x12
#define D_l   x13
#define D_h   x14

	.macro def_fn f p2align=0
	.text
	.p2align \p2align
	.global \f
	.type \f, %function
\f:
	.endm

def_fn memcpy p2align=6

	mov	dst, dstin
	cmp	count, #64
	b.ge	.Lcpy_not_short
	cmp	count, #15
	b.le	.Ltail15tiny

	/* Deal with small copies quickly by dropping straight into the
	 * exit block.  */
.Ltail63:
	/* Copy up to 48 bytes of data.  At this point we only need the
	 * bottom 6 bits of count to be accurate.  */
	ands	tmp1, count, #0x30
	b.eq	.Ltail15
	add	dst, dst, tmp1
	add	src, src, tmp1
	cmp	tmp1w, #0x20
	b.eq	1f
	b.lt	2f
	ldp	A_l, A_h, [src, #-48]
	stp	A_l, A_h, [dst, #-48]
1:
	ldp	A_l, A_h, [src, #-32]
	stp	A_l, A_h, [dst, #-32]
2:
	ldp	A_l, A_h, [src, #-16]
	stp	A_l, A_h, [dst, #-16]

.Ltail15:
	ands	count, count, #15
	beq	1f
	add	src, src, count
	ldp	A_l, A_h, [src, #-16]
	add	dst, dst, count
	stp	A_l, A_h, [dst, #-16]
1:
	ret

.Ltail15tiny:
	/* Copy up to 15 bytes of data.  Does not assume additional data
	   being copied.  */
	tbz	count, #3, 1f
	ldr	tmp1, [src], #8
	str	tmp1, [dst], #8
1:
	tbz	count, #2, 1f
	ldr	tmp1w, [src], #4
	str	tmp1w, [dst], #4
1:
	tbz	count, #1, 1f
	ldrh	tmp1w, [src], #2
	strh	tmp1w, [dst], #2
1:
	tbz	count, #0, 1f
	ldrb	tmp1w, [src]
	strb	tmp1w, [dst]
1:
	ret

.Lcpy_not_short:
	/* We don't much care about the alignment of DST, but we want SRC
	 * to be 128-bit (16 byte) aligned so that we don't cross cache line
	 * boundaries on both loads and stores.  */
	neg	tmp2, src
	ands	tmp2, tmp2, #15		/* Bytes to reach alignment.  */
	b.eq	2f
	sub	count, count, tmp2
	/* Copy more data than needed; it's faster than jumping
	 * around copying sub-Quadword quantities.  We know that
	 * it can't overrun.  */
	ldp	A_l, A_h, [src]
	add	src, src, tmp2
	stp	A_l, A_h, [dst]
	add	dst, dst, tmp2
	/* There may be less than 63 bytes to go now.  */
	cmp	count, #63
	b.le	.Ltail63
2:
	subs	count, count, #128
	b.ge	.Lcpy_body_large
	/* Less than 128 bytes to copy, so handle 64 here and then jump
	 * to the tail.  */
	ldp	A_l, A_h, [src]
	ldp	B_l, B_h, [src, #16]
	ldp	C_l, C_h, [src, #32]
	ldp	D_l, D_h, [src, #48]
	stp	A_l, A_h, [dst]
	stp	B_l, B_h, [dst, #16]
	stp	C_l, C_h, [dst, #32]
	stp	D_l, D_h, [dst, #48]
	tst	count, #0x3f
	add	src, src, #64
	add	dst, dst, #64
	b.ne	.Ltail63
	ret

	/* Critical loop.  Start at a new cache line boundary.  Assuming
	 * 64 bytes per line this ensures the entire loop is in one line.  */
	.p2align 6
.Lcpy_body_large:
	/* There are at least 128 bytes to copy.  */
	ldp	A_l, A_h, [src, #0]
	sub	dst, dst, #16		/* Pre-bias.  */
	ldp	B_l, B_h, [src, #16]
	ldp	C_l, C_h, [src, #32]
	ldp	D_l, D_h, [src, #48]!	/* src += 64 - Pre-bias.  */
1:
	stp	A_l, A_h, [dst, #16]
	ldp	A_l, A_h, [src, #16]
	stp	B_l, B_h, [dst, #32]
	ldp	B_l, B_h, [src, #32]
	stp	C_l, C_h, [dst, #48]
	ldp	C_l, C_h, [src, #48]
	stp	D_l, D_h, [dst, #64]!
	ldp	D_l, D_h, [src, #64]!
	subs	count, count, #64
	b.ge	1b
	stp	A_l, A_h, [dst, #16]
	stp	B_l, B_h, [dst, #32]
	stp	C_l, C_h, [dst, #48]
	stp	D_l, D_h, [dst, #64]
	add	src, src, #16
	add	dst, dst, #64 + 16
	tst	count, #0x3f
	b.ne	.Ltail63
	ret
	.size	memcpy, .-memcpy

#undef dst
#undef tmp3w

#define dstin x0
#define val w1
#define count x2
#define tmp1 x3
#define tmp1w w3
#define tmp2 x4
#define tmp2w w4
#define zva_len_x x5
#define zva_len w5
#define zva_bits_x x6

#define A_l x7
#define A_lw w7

#define dst x8
#define tmp3w w9

def_fn memset p2align=6

	mov	dst, dstin		/* Preserve return value.  */
	ands	A_lw, val, #255
#ifndef DONT_USE_DC
	b.eq	.Lzero_mem
#endif
	orr	A_lw, A_lw, A_lw, lsl #8
	orr	A_lw, A_lw, A_lw, lsl #16
	orr	A_l, A_l, A_l, lsl #32
.Ltail_maybe_long:
	cmp	count, #64
	b.ge	.Lnot_short
.Ltail_maybe_tiny:
	cmp	count, #15
	b.le	.Ltail15_memsettiny
.Ltail63_memset:
	ands	tmp1, count, #0x30
	b.eq	.Ltail15_memset
	add	dst, dst, tmp1
	cmp	tmp1w, #0x20
	b.eq	1f
	b.lt	2f
	stp	A_l, A_l, [dst, #-48]
1:
	stp	A_l, A_l, [dst, #-32]
2:
	stp	A_l, A_l, [dst, #-16]

.Ltail15_memset:
	and	count, count, #15
	add	dst, dst, count
	stp	A_l, A_l, [dst, #-16]	/* Repeat some/all of last store. */
	ret

.Ltail15_memsettiny:
	/* Set up to 15 bytes.  Does not assume earlier memory
	   being set.  */
	tbz	count, #3, 1f
	str	A_l, [dst], #8
1:
	tbz	count, #2, 1f
	str	A_lw, [dst], #4
1:
	tbz	count, #1, 1f
	strh	A_lw, [dst], #2
1:
	tbz	count, #0, 1f
	strb	A_lw, [dst]
1:
	ret

	/* Critical loop.  Start at a new cache line boundary.  Assuming
	 * 64 bytes per line, this ensures the entire loop is in one line.  */
	.p2align 6
.Lnot_short:
	neg	tmp2, dst
	ands	tmp2, tmp2, #15
	b.eq	2f
	/* Bring DST to 128-bit (16-byte) alignment.  We know that there's
	 * more than that to set, so we simply store 16 bytes and advance by
	 * the amount required to reach alignment.  */
	sub	count, count, tmp2
	stp	A_l, A_l, [dst]
	add	dst, dst, tmp2
	/* There may be less than 63 bytes to go now.  */
	cmp	count, #63
	b.le	.Ltail63_memset
2:
	sub	dst, dst, #16		/* Pre-bias.  */
	sub	count, count, #64
1:
	stp	A_l, A_l, [dst, #16]
	stp	A_l, A_l, [dst, #32]
	stp	A_l, A_l, [dst, #48]
	stp	A_l, A_l, [dst, #64]!
	subs	count, count, #64
	b.ge	1b
	tst	count, #0x3f
	add	dst, dst, #16
	b.ne	.Ltail63_memset
	ret

#ifndef DONT_USE_DC
	/* For zeroing memory, check to see if we can use the ZVA feature to
	 * zero entire 'cache' lines.  */
.Lzero_mem:
	mov	A_l, #0
	cmp	count, #63
	b.le	.Ltail_maybe_tiny
	neg	tmp2, dst
	ands	tmp2, tmp2, #15
	b.eq	1f
	sub	count, count, tmp2
	stp	A_l, A_l, [dst]
	add	dst, dst, tmp2
	cmp	count, #63
	b.le	.Ltail63_memset
1:
	/* For zeroing small amounts of memory, it's not worth setting up
	 * the line-clear code.  */
	cmp	count, #128
	b.lt	.Lnot_short
#ifdef MAYBE_VIRT
	/* For efficiency when virtualized, we cache the ZVA capability.  */
	adrp	tmp2, .Lcache_clear
	ldr	zva_len, [tmp2, #:lo12:.Lcache_clear]
	tbnz	zva_len, #31, .Lnot_short
	cbnz	zva_len, .Lzero_by_line
	mrs	tmp1, dczid_el0
	tbz	tmp1, #4, 1f
	/* ZVA not available.  Remember this for next time.  */
	mov	zva_len, #~0
	str	zva_len, [tmp2, #:lo12:.Lcache_clear]
	b	.Lnot_short
1:
	mov	tmp3w, #4
	and	zva_len, tmp1w, #15	/* Safety: other bits reserved.  */
	lsl	zva_len, tmp3w, zva_len
	str	zva_len, [tmp2, #:lo12:.Lcache_clear]
#else
	mrs	tmp1, dczid_el0
	tbnz	tmp1, #4, .Lnot_short
	mov	tmp3w, #4
	and	zva_len, tmp1w, #15	/* Safety: other bits reserved.  */
	lsl	zva_len, tmp3w, zva_len
#endif

.Lzero_by_line:
	/* Compute how far we need to go to become suitably aligned.  We're
	 * already at quad-word alignment.  */
	cmp	count, zva_len_x
	b.lt	.Lnot_short		/* Not enough to reach alignment.  */
	sub	zva_bits_x, zva_len_x, #1
	neg	tmp2, dst
	ands	tmp2, tmp2, zva_bits_x
	b.eq	1f			/* Already aligned.  */
	/* Not aligned, check that there's enough to copy after alignment.  */
	sub	tmp1, count, tmp2
	cmp	tmp1, #64
	ccmp	tmp1, zva_len_x, #8, ge	/* NZCV=0b1000 */
	b.lt	.Lnot_short
	/* We know that there's at least 64 bytes to zero and that it's safe
	 * to overrun by 64 bytes.  */
	mov	count, tmp1
2:
	stp	A_l, A_l, [dst]
	stp	A_l, A_l, [dst, #16]
	stp	A_l, A_l, [dst, #32]
	subs	tmp2, tmp2, #64
	stp	A_l, A_l, [dst, #48]
	add	dst, dst, #64
	b.ge	2b
	/* We've overrun a bit, so adjust dst downwards.  */
	add	dst, dst, tmp2
1:
	sub	count, count, zva_len_x
3:
	dc	zva, dst
	add	dst, dst, zva_len_x
	subs	count, count, zva_len_x
	b.ge	3b
	ands	count, count, zva_bits_x
	b.ne	.Ltail_maybe_long
	ret
	.size	memset, .-memset
#ifdef MAYBE_VIRT
	.bss
	.p2align 2
.Lcache_clear:
	.space 4
#endif
#endif /* DONT_USE_DC */

#undef dst
#undef tmp3w
/* Parameters and result.  */
#define dstin	x0
#define src	x1
#define count	x2
#define tmp1	x3
#define tmp1w	w3
#define tmp2	x4
#define tmp2w	w4
#define tmp3	x5
#define tmp3w	w5
#define dst	x6

#define A_l	x7
#define A_h	x8
#define B_l	x9
#define B_h	x10
#define C_l	x11
#define C_h	x12
#define D_l	x13
#define D_h	x14

/* Upwards move with potential overlap.
 * Need to move from the tail backwards.  SRC and DST point one
 * byte beyond the remaining data to move.  */
def_fn memcpy_backwards p2align=6
	add	dst, dstin, count
	add	src, src, count
	cmp	count, #64
	b.ge	.Lmov_not_short_up

	/* Deal with small moves quickly by dropping straight into the
	 * exit block.  */
.Ltail63up:
	/* Move up to 48 bytes of data.  At this point we only need the
	 * bottom 6 bits of count to be accurate.  */
	ands	tmp1, count, #0x30
	b.eq	.Ltail15up
	sub	dst, dst, tmp1
	sub	src, src, tmp1
	cmp	tmp1w, #0x20
	b.eq	1f
	b.lt	2f
	ldp	A_l, A_h, [src, #32]
	stp	A_l, A_h, [dst, #32]
1:
	ldp	A_l, A_h, [src, #16]
	stp	A_l, A_h, [dst, #16]
2:
	ldp	A_l, A_h, [src]
	stp	A_l, A_h, [dst]
.Ltail15up:
	/* Move up to 15 bytes of data.  Does not assume additional data
	 * being moved.  */
	tbz	count, #3, 1f
	ldr	tmp1, [src, #-8]!
	str	tmp1, [dst, #-8]!
1:
	tbz	count, #2, 1f
	ldr	tmp1w, [src, #-4]!
	str	tmp1w, [dst, #-4]!
1:
	tbz	count, #1, 1f
	ldrh	tmp1w, [src, #-2]!
	strh	tmp1w, [dst, #-2]!
1:
	tbz	count, #0, 1f
	ldrb	tmp1w, [src, #-1]
	strb	tmp1w, [dst, #-1]
1:
	ret

.Lmov_not_short_up:
	/* We don't much care about the alignment of DST, but we want SRC
	 * to be 128-bit (16 byte) aligned so that we don't cross cache line
	 * boundaries on both loads and stores.  */
	ands	tmp2, src, #15		/* Bytes to reach alignment.  */
	b.eq	2f
	sub	count, count, tmp2
	/* Move enough data to reach alignment; unlike memcpy, we have to
	 * be aware of the overlap, which means we can't move data twice.  */
	tbz	tmp2, #3, 1f
	ldr	tmp1, [src, #-8]!
	str	tmp1, [dst, #-8]!
1:
	tbz	tmp2, #2, 1f
	ldr	tmp1w, [src, #-4]!
	str	tmp1w, [dst, #-4]!
1:
	tbz	tmp2, #1, 1f
	ldrh	tmp1w, [src, #-2]!
	strh	tmp1w, [dst, #-2]!
1:
	tbz	tmp2, #0, 1f
	ldrb	tmp1w, [src, #-1]!
	strb	tmp1w, [dst, #-1]!
1:

	/* There may be less than 63 bytes to go now.  */
	cmp	count, #63
	b.le	.Ltail63up
2:
	subs	count, count, #128
	b.ge	.Lmov_body_large_up
	/* Less than 128 bytes to move, so handle 64 here and then jump
	 * to the tail.  */
	ldp	A_l, A_h, [src, #-64]!
	ldp	B_l, B_h, [src, #16]
	ldp	C_l, C_h, [src, #32]
	ldp	D_l, D_h, [src, #48]
	stp	A_l, A_h, [dst, #-64]!
	stp	B_l, B_h, [dst, #16]
	stp	C_l, C_h, [dst, #32]
	stp	D_l, D_h, [dst, #48]
	tst	count, #0x3f
	b.ne	.Ltail63up
	ret

	/* Critical loop.  Start at a new Icache line boundary.  Assuming
	 * 64 bytes per line this ensures the entire loop is in one line.  */
	.p2align 6
.Lmov_body_large_up:
	/* There are at least 128 bytes to move.  */
	ldp	A_l, A_h, [src, #-16]
	ldp	B_l, B_h, [src, #-32]
	ldp	C_l, C_h, [src, #-48]
	ldp	D_l, D_h, [src, #-64]!
1:
	stp	A_l, A_h, [dst, #-16]
	ldp	A_l, A_h, [src, #-16]
	stp	B_l, B_h, [dst, #-32]
	ldp	B_l, B_h, [src, #-32]
	stp	C_l, C_h, [dst, #-48]
	ldp	C_l, C_h, [src, #-48]
	stp	D_l, D_h, [dst, #-64]!
	ldp	D_l, D_h, [src, #-64]!
	subs	count, count, #64
	b.ge	1b
	stp	A_l, A_h, [dst, #-16]
	stp	B_l, B_h, [dst, #-32]
	stp	C_l, C_h, [dst, #-48]
	stp	D_l, D_h, [dst, #-64]!
	tst	count, #0x3f
	b.ne	.Ltail63up
	ret
	.size memcpy_backwards, . - memcpy_backwards
