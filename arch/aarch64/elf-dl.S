/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "macros.S"

.text

.global __elf_resolve_pltgot

// calling convention:
// ip0: &pltgot(2)
// ip1: &__elf_resolve_pltgot
// [sp, #8] lr
// [sp, #0] &PLTGOT[n]

__elf_resolve_pltgot:
.type __elf_resolve_pltgot, @function
        stp     x8, xzr, [sp, #-80]! // space for x0-x8
        stp     x6, x7, [sp, #16]
        stp     x4, x5, [sp, #32]
        stp     x2, x3, [sp, #48]
        stp     x0, x1, [sp, #64]

        ldr     x0, [sp, #80]       // load &PLTGOT[n]
        ldr     x1, [x16, #-8]      // load object
        sub     x0, x0, x16         // &PLTGOT[n] - &PLTGOT[2]
        lsr     x0, x0, #3
        sub     x0, x0, #1          // x0 = index

        bl      elf_resolve_pltgot
        mov     x16, x0

	ldp	x0, x1, [sp, #64]
	ldp	x2, x3, [sp, #48]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #16]
	ldp	x8, xzr, [sp], #(80)

        pop_pair x17, x30
        br      x16
