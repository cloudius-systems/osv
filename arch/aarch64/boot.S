/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

.text
.align 16
.globl start_elf
.hidden start_elf
.type start_elf , "function"
start_elf:
        /* elf program start address */
        /* input: x3=elf header x4=cmdline x5=dtb */
        adrp    x0, exception_vectors
        msr     vbar_el1, x0
        isb

        bl      validate_el // check that we are at EL1 or die
        bl      init_stack
        bl      zero_bss    // initialize bss contents to 0
        bl      init_cpu
        bl      init_boot_pt
        bl      init_mmu

        adrp    x1, elf_header              // store elf header address
        str     x3, [x1, #:lo12:elf_header]
        adrp    x1, cmdline                 // store cmdline (arch-setup.cc)
        str     x4, [x1, #:lo12:cmdline]
        adrp    x1, dtb                     // store dtb (arch-setup.cc)
        str     x5, [x1, #:lo12:dtb]

        mov     x29, xzr
        bl      init_xen

        mov     x29, xzr
        bl      premain

        adrp    x3, __loader_argc
        ldr     w0, [x3, #:lo12:__loader_argc]
        adrp    x3, __loader_argv
        ldr     x1, [x3, #:lo12:__loader_argv]
        bl      main

        bl      halt

init_stack:
        mov     x0, #1     // select SP_ELx
        msr     spsel, x0
        isb

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

init_cpu:
        ic      iallu
        tlbi    vmalle1
        dsb     sy

        mov     x0, #3 << 20
        msr     cpacr_el1, x0  // no trapping on FP/SIMD instructions
        msr     mdscr_el1, xzr // monitor debug: all disabled
        isb

        /* MAIR_EL1 contains 8 entries (one per byte) defining attributes
           which we will then reference from the page tables.
           We will currently use only Attr[4] for everything, but
           keep in mind that for direct device assignment we might need
           to use the others too, and adapt the pt_element API */
        ldr     x0, =0xff440c0400
        msr	mair_el1, x0
        isb

        /* TCR - Translation Control Register
         *
         * 63                  39   38   37  36   35    34   32
         * [        RES0         ] TBI1 TBI0 AS [RES0] [  IPS  ]
         *                           0    0   1    0     1 0 1
         *  31 30   29 28   27 26   25 24   23    22   21      16
         * [ TG1 ] [ SH1 ] [ORGN1] [IRGN1] EPD1   A1   [  T1SZ   ]
         *   1  0    1  1    0  1    0  1    0     0     010000
         *  15 14   13 12   11 10    9  8    7     6    5       0
         * [ TG0 ] [ SH0 ] [ORGN0] [IRGN0] EPD0 [RES0] [  T0SZ   ]
         *   0  0    1  1    0  1    0  1    0     0     010000
         */

        /* we choose 4K granularity, 48-bit addresses, two subranges:
         *
         * TTBR1_EL1 -> 0xffff_0000_0000_0000 to 0xffff_ffff_ffff_ffff
         * TTBR0_EL1 -> 0x0000_0000_0000_0000 to 0x0000_ffff_ffff_ffff
         * ASID = 16bit TTBR
         */
        ldr     x0, =0x15b5103510
        msr     tcr_el1, x0
        isb
        ret

init_boot_pt:
        adrp    x0, ident_pt_l4_ttbr0
        adrp    x1, ident_pt_l4_ttbr1
        ret

init_secondary_pt:
        adrp    x1, smpboot_ttbr0
        ldr     x0, [x1], #8
        ldr     x1, [x1]
        ret

        /* call with x0, x1 containing ttbr0, ttbr1 to set */
init_mmu:
        msr     ttbr0_el1, x0
        msr     ttbr1_el1, x1
        isb

        /* SCTRL_EL1 - System Control Register (EL1)
         *
         *   31 30 29 28 27   26   25  24  23 22 21 20  19   18   17  16
         *                  [UCI] [EE E0E]             [WXN][nTWE]   [nTWI]
         *    0  0  1  1  0    0    0   0   1  1  0  1   0    0    0   0
         *
         *   15  14  13 12 11 10   9    8    7    6    5   4    3  2  1  0
         * [UCT][DZE]   [I]      [UMA][SED][ITD][THE][CP][SA0][SA][C][A][M]
         *    0   0   0  1  1  0   0    1    1    0    0   1    1  1  0  1
         */
        ldr     x0, =0x30d0199d
        msr     sctlr_el1, x0
        isb

        ret

.align 16
.globl start_secondary_cpu
.hidden start_secondary_cpu
.type  start_secondary_cpu , "function"
start_secondary_cpu:
        adrp    x0, exception_vectors
        msr     vbar_el1, x0
        isb

        bl      init_cpu
        bl      init_boot_pt
        bl      init_mmu
        bl      init_secondary_pt
        bl      init_mmu

        ldr     x0, =smp_stack_free    /* ptr */
        ldr     x1, [x0]               /* old value */
        ldr     x2, [x1, #4096]        /* new value */
        dmb     ish
1:
        ldxr    x3, [x0]
        cmp     x3, x1
        b.ne    2f
        stxr    w4, x2, [x0]
        cbnz    w4, 1b
2:
        dmb     ish
        mov     x0, #1     // select SP_ELx
        msr     spsel, x0
        isb
        add     x1, x1, #4096
        mov     sp, x1
        mov     x29, xzr
        bl      smp_main
        bl      halt

.align 12
ident_pt_l4_ttbr0:
        .quad ident_pt_l3_ttbr0 + 0x3
        .rept 511
        .quad 0
        .endr
ident_pt_l3_ttbr0:
        .quad ident_pt_l2_0_ttbr0 + 0x3 // Map 0GB-1GB one-to-one
        .quad ident_pt_l2_1_ttbr0 + 0x3 // Map 1GB-2GB one-to-one
        .quad ident_pt_l2_2_ttbr0 + 0x3 // Map 2GB-3GB one-to-one
        .quad ident_pt_l2_3_ttbr0 + 0x3 // Map 3GB-4GB one-to-one
        .rept 508
        .quad 0
        .endr
ident_pt_l2_0_ttbr0:
        index = 0
        offset = 0x00000000
        .rept 512
        .quad offset + (index << 21) + 0x411
        index = index + 1
        .endr
ident_pt_l2_1_ttbr0:
        index = 0
        offset = 0x40000000
        .rept 512
        .quad offset + (index << 21) + 0x411
        index = index + 1
        .endr
ident_pt_l2_2_ttbr0:
        index = 0
        offset = 0x80000000
        .rept 512
        .quad offset + (index << 21) + 0x411
        index = index + 1
        .endr
ident_pt_l2_3_ttbr0:
        index = 0
        offset = 0xC0000000
        .rept 512
        .quad offset + (index << 21) + 0x411
        index = index + 1
        .endr
.align 12
ident_pt_l4_ttbr1:
        .rept 512
        .quad 0
        .endr

.bss
.align 16
init_stack_bottom = .
. = . + 4096*4
init_stack_top = .

.globl smp_stack_free
.hidden smp_stack_free
.type smp_stack_free , "object"
smp_stack_free: .quad 0

.globl smpboot_ttbr0
.hidden smpboot_ttbr0
.type smpboot_ttbr0 , "object"
.hidden smpboot_ttbr1
.globl smpboot_ttbr1
.type smpboot_ttbr1 , "object"

.align 12
smpboot_ttbr0:  .quad 0
smpboot_ttbr1:  .quad 0

/* hmm should we provide an interrupt stack?
. = . + 4096*10
.global interrupt_stack_top
interrupt_stack_top = .
*/
