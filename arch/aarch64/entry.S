/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "macros.S"

/* the exception vectors area must be page aligned (we adrp directly).

   From table D1-6 of the ARM TR Manual:

        Table D1-6 shows this:
        Table D1-6 Vector offsets from vector table base address
                                                                   Offset for exception type
        Exception taken from
                                                         Synchr   IRQ/vIRQ  [v]FIQ   [v]SError
        Current Exception level with SP_EL0.             0x000     0x080     0x100     0x180
        Current Exception level with SP_ELx, x>0.        0x200     0x280     0x300     0x380
        Lower Exception level, from AArch64              0x400     0x480     0x500     0x580
        Lower Exception level, from AArch32              0x600     0x680     0x700     0x780
 */

.macro vector_entry label idx
/* every entry is at 2^7 bits distance */
.align 7
        b       \label
.endm

.global exception_vectors
.type exception_vectors, @function
.align 12
exception_vectors:
        /* Current Exception level with SP_EL0 : unused */
        vector_entry entry_invalid  0   // Synchronous
        vector_entry entry_invalid  1   // IRQ or vIRQ
        vector_entry entry_invalid  2   // FIQ or vFIQ
        vector_entry entry_invalid  3   // SError or vSError

        /* Current Exception level with SP_ELx : only actually used */
        vector_entry entry_sync     4
        vector_entry entry_irq      5
        vector_entry entry_fiq      6
        vector_entry entry_serror   7

        /* Lower Exception level in AArch64 : unused since we don't go to EL0 */
        vector_entry entry_invalid  8
        vector_entry entry_invalid  9
        vector_entry entry_invalid 10
        vector_entry entry_invalid 11

        /* Lower Exception level in AArch32 : no El0, no AArch32 */
        vector_entry entry_invalid 12
        vector_entry entry_invalid 13
        vector_entry entry_invalid 14
        vector_entry entry_invalid 15

/* keep in sync with the struct in exceptions.hh */
.macro push_state_to_exception_frame
        sub     sp, sp, #48 // make space for align2, align1+ESR, PSTATE, PC, SP
        push_pair x28, x29
        push_pair x26, x27
        push_pair x24, x25
        push_pair x22, x23
        push_pair x20, x21
        push_pair x18, x19
        push_pair x16, x17
        push_pair x14, x15
        push_pair x12, x13
        push_pair x10, x11
        push_pair x8, x9
        push_pair x6, x7
        push_pair x4, x5
        push_pair x2, x3
        push_pair x0, x1
        add     x1, sp, #288         // x1 := old SP (48 + 16 * 15 = 288)
        mrs     x2, elr_el1
        mrs     x3, spsr_el1
        stp     x30, x1, [sp, #240]  // store lr, old SP
        stp     x2, x3, [sp, #256]   // store elr_el1, spsr_el1
.endm /* push_state_to_exception_frame */

.macro pop_state_from_exception_frame
        ldp     x21, x22, [sp, #256] // load elr_el1, spsr_el1
        pop_pair x0, x1
        pop_pair x2, x3
        pop_pair x4, x5
        pop_pair x6, x7
        pop_pair x8, x9
        msr     elr_el1, x21         // set loaded elr and spsr
        msr     spsr_el1, x22
        pop_pair x10, x11
        pop_pair x12, x13
        pop_pair x14, x15
        pop_pair x16, x17
        pop_pair x18, x19
        pop_pair x20, x21
        pop_pair x22, x23
        pop_pair x24, x25
        pop_pair x26, x27
        pop_pair x28, x29
        ldr     x30, [sp], #48
.endm /* pop_state_to_exception_frame */

.equ ESR_EC_BEG,26          // Exception Class field begin in ESR
.equ ESR_EC_END,31          // Exception Class field end in ESR
.equ ESR_EC_DATA_ABORT,0x25 // Exception Class Data Abort value
.equ ESR_EC_INSN_ABORT,0x21 // Exception Class Instruction Abort value

.equ ESR_ISS_BEG,0          // Instruction-Specific Syndrome field begin in ESR
.equ ESR_ISS_END,23         // Instruction-Specific Syndrome field end in ESR

// Faults are fault status codes 1, 2 and 3. Applies to INSN and DATA abort.
// Translation Fault = 0b0001LL
// Access Flag Fault = 0b0010LL
// Permission Fault  = 0b0011LL
.equ ESR_FLT_BEG,2 // we strip LL
.equ ESR_FLT_END,5

.global entry_invalid
.type entry_invalid, @function
entry_invalid:
        mrs     x20, elr_el1       // Exception Link Register -> X20
        mrs     x21, spsr_el1      // Saved PSTATE -> X21
        mrs     x22, esr_el1       // Exception Syndrome Register -> X22

        ubfm    x23, x22, #ESR_EC_BEG, #ESR_EC_END   // Exception Class -> X23
        ubfm    x24, x22, #ESR_ISS_BEG, #ESR_ISS_END // Instruction-Specific Syndrome -> X24

1:      wfi
        b       1b

.global entry_sync
.type entry_sync, @function
entry_sync:
        push_state_to_exception_frame
        mrs     x1, esr_el1
        str     w1, [sp, #272] // Store Exception Syndrom Register in the frame
        ubfm    x2, x1, #ESR_EC_BEG, #ESR_EC_END // Exception Class -> X2
        ubfm    x3, x1, #ESR_FLT_BEG, #ESR_FLT_END // FLT -> X3
        cmp     x2, #ESR_EC_DATA_ABORT
        b.eq    handle_mem_abort
        cmp     x2, #ESR_EC_INSN_ABORT
        b.ne    unexpected_sync_exception
handle_mem_abort:
        cbz     x3, unexpected_sync_exception
        cmp     x3, #3
        b.hi    unexpected_sync_exception

        mov     x0, sp  // save exception_frame to x0
        bl      page_fault
        pop_state_from_exception_frame
        eret
unexpected_sync_exception:
        mov     x0, sp  // save exception_frame to x0
        bl      handle_unexpected_sync_exception
        pop_state_from_exception_frame
        bl      abort

.global entry_irq
.type entry_irq, @function
entry_irq:
        push_state_to_exception_frame
        mov     x0, sp
        bl      interrupt // extern "C"
        pop_state_from_exception_frame
        eret

.global entry_fiq
.type entry_fiq, @function
entry_fiq:
.global entry_serror
.type entry_serror, @function
entry_serror:
        b       entry_invalid

.global call_signal_handler_thunk
call_signal_handler_thunk:
        .type call_signal_handler_thunk, @function
        # stack contains a signal_frame
        /*
        .cfi_offset reg, offset
        ...
        mov x0, sp
        call call_signal_handler
        # FIXME: fpu

        pop_pair...
        add sp, sp, 16 # error_code
        */
        ret

// Keep fpu_state_save/load in sync with struct fpu_state in arch/aarch64/processor.hh
// void fpu_state_save(fpu_state *s);
.global fpu_state_save
.type fpu_state_save, @function
fpu_state_save:
        stp     q0, q1, [x0]
        stp     q2, q3, [x0, #32]
        stp     q4, q5, [x0, #64]
        stp     q6, q7, [x0, #96]
        stp     q8, q9, [x0, #128]
        stp     q10, q11, [x0, #160]
        stp     q12, q13, [x0, #192]
        stp     q14, q15, [x0, #224]
        stp     q16, q17, [x0, #256]
        stp     q18, q19, [x0, #288]
        stp     q20, q21, [x0, #320]
        stp     q22, q23, [x0, #352]
        stp     q24, q25, [x0, #384]
        stp     q26, q27, [x0, #416]
        stp     q28, q29, [x0, #448]
        stp     q30, q31, [x0, #480]
        mrs     x1, fpsr
        str     w1, [x0, #512]
        mrs     x1, fpcr
        str     w1, [x0, #516]
        ret

// void fpu_state_load(fpu_state *s);
.global fpu_state_load
.type fpu_state_load, @function
fpu_state_load:
        ldp     q0, q1, [x0]
        ldp     q2, q3, [x0, #32]
        ldp     q4, q5, [x0, #64]
        ldp     q6, q7, [x0, #96]
        ldp     q8, q9, [x0, #128]
        ldp     q10, q11, [x0, #160]
        ldp     q12, q13, [x0, #192]
        ldp     q14, q15, [x0, #224]
        ldp     q16, q17, [x0, #256]
        ldp     q18, q19, [x0, #288]
        ldp     q20, q21, [x0, #320]
        ldp     q22, q23, [x0, #352]
        ldp     q24, q25, [x0, #384]
        ldp     q26, q27, [x0, #416]
        ldp     q28, q29, [x0, #448]
        ldp     q30, q31, [x0, #480]
        ldr     w1, [x0, #512]
        msr     fpsr, x1
        ldr     w1, [x0, #516]
        msr     fpcr, x1
        ret
