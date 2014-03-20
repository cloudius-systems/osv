/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

.text
.align 16
.globl start_elf
start_elf:
        /* elf program start address */
        /* input: x0 = elf header address */
        mov     x2, x0      // elf_header ptr => x2
        bl      validate_el // check that we are at EL1 or die

        bl      init_stack
        bl      zero_bss    // initialize bss contents to 0
        bl      init_boot_cpu

        adrp    x1, elf_header // requires page alignment */
        str     x2, [x1]    // store elf header address

        mov     x29, xzr
        mov     x30, xzr
        bl      premain
        /*
        adrp    x2, __argc
        ldr     x0, [x2]
        adrp    x2, __argv
        ldr     x1, [x2]
        b       main
        */

init_stack:
        adrp    x0, init_stack_top
        mov     sp, x0
        ret

validate_el:
	mrs     x0, currentel
        ubfm    x0, x0, #2, #3 // current EL[3:2] -> X0
        cmp     x0, #1
        b.ne    halt
        ret

halt:   wfi
        b       halt

zero_bss:
        adrp    x0, .bss
        adrp    x1, .edata
zero_bss_loop:
        stp     xzr, xzr, [x0], #16
        cmp     x0, x1
        b.lo    zero_bss_loop
        ret
init_boot_cpu:
        mov x0, #3 << 20
        msr cpacr_el1, x0  // no trapping on FP/SIMD instructions
        msr mdscr_el1, xzr // monitor debug: all disabled
        isb
        ret

.bss
.align 16
init_stack_bottom = .
. = . + 4096*4
init_stack_top = .

/* hmm should we provide an interrupt stack?
. = . + 4096*10
.global interrupt_stack_top
interrupt_stack_top = .
*/
