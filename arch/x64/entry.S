# Copyright (C) 2013 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.


#include "cfi.S"

.macro exception_entry name, handler, has_error_code
	.global \name
        .type \name, @function
	\name :
	.cfi_startproc simple
	.cfi_signal_frame
	.if \has_error_code == 0
	pushq $0
	.endif
	.cfi_def_cfa %rsp, 0
	.cfi_offset %rip, 8
	.cfi_offset %rsp, 32
	pushq_cfi %rax
	pushq_cfi %rbx
	pushq_cfi %rcx
	pushq_cfi %rdx
	pushq_cfi %rsi
	pushq_cfi %rdi
	pushq_cfi %rbp
	pushq_cfi %r8
	pushq_cfi %r9
	pushq_cfi %r10
	pushq_cfi %r11
	pushq_cfi %r12
	pushq_cfi %r13
	pushq_cfi %r14
	pushq_cfi %r15
	mov %rsp, %rdi
	subq $8, %rsp # 16-byte alignment
	.cfi_adjust_cfa_offset 8
	call \handler
	addq $8, %rsp # 16-byte alignment
	.cfi_adjust_cfa_offset -8
	popq_cfi %r15
	popq_cfi %r14
	popq_cfi %r13
	popq_cfi %r12
	popq_cfi %r11
	popq_cfi %r10
	popq_cfi %r9
	popq_cfi %r8
	popq_cfi %rbp
	popq_cfi %rdi
	popq_cfi %rsi
	popq_cfi %rdx
	popq_cfi %rcx
	popq_cfi %rbx
	popq_cfi %rax
	add $8, %rsp
	iretq
	.cfi_endproc
.endm

.macro exception_error_entry name, handler
	exception_entry \name, \handler, 1
.endm

.macro exception_noerror_entry name, handler
	exception_entry \name, \handler, 0
.endm

.cfi_sections .eh_frame,  .debug_frame

.text

exception_noerror_entry ex_de, divide_error
exception_noerror_entry ex_db, debug_exception
exception_noerror_entry ex_nmi, nmi
exception_noerror_entry ex_bp, breakpoint
exception_noerror_entry ex_of, overflow
exception_noerror_entry ex_br, bound_range_exceeded
exception_noerror_entry ex_ud, invalid_opcode
exception_noerror_entry ex_nm, device_not_available
exception_error_entry ex_df, double_fault
exception_error_entry ex_ts, invalid_tss
exception_error_entry ex_np, segment_not_present,
exception_error_entry ex_ss, stack_fault
exception_error_entry ex_gp, general_protection
exception_error_entry ex_pf, page_fault
exception_noerror_entry ex_mf, math_fault
exception_error_entry ex_ac, alignment_check
exception_noerror_entry ex_mc, machine_check
exception_noerror_entry ex_xm, simd_exception

.align 16
.global interrupt_entry
interrupt_entry:
vector = 32
.rept 256 - 32
    .align 16
    pushq $vector
    jmp interrupt_entry_common
    vector = vector + 1
.endr

exception_error_entry interrupt_entry_common, interrupt

.global thread_main
thread_main:
        .type thread_main, @function
	.cfi_startproc simple
	.cfi_undefined %rip
	.cfi_def_cfa %rsp, 0
	mov %rbp, %rdi
	call thread_main_c
	.cfi_endproc

.global call_signal_handler_thunk
call_signal_handler_thunk:
        .type call_signal_handler_thunk, @function
        .cfi_startproc simple
        .cfi_signal_frame
        .cfi_def_cfa %rsp, 0
        # stack contains a signal_frame
        .cfi_offset %r15, 0x00
        .cfi_offset %r14, 0x08
        .cfi_offset %r13, 0x10
        .cfi_offset %r12, 0x18
        .cfi_offset %r11, 0x20
        .cfi_offset %r10, 0x28
        .cfi_offset %r9, 0x30
        .cfi_offset %r8, 0x38
        .cfi_offset %rbp, 0x40
        .cfi_offset %rdi, 0x48
        .cfi_offset %rsi, 0x50
        .cfi_offset %rdx, 0x58
        .cfi_offset %rcx, 0x60
        .cfi_offset %rbx, 0x68
        .cfi_offset %rax, 0x70
        .cfi_offset %rip, 0x80
        .cfi_offset %rsp, 0x98
        mov %rsp, %rdi
        call call_signal_handler
        # FIXME: fpu
        pop %r15
        pop %r14
        pop %r13
        pop %r12
        pop %r11
        pop %r10
        pop %r9
        pop %r8
        pop %rbp
        pop %rdi
        pop %rsi
        pop %rdx
        pop %rcx
        pop %rbx
        pop %rax
        add $8, %rsp # error_core
        iretq
        .cfi_endproc

.global stack_trampoline
stack_trampoline:  # %rsi=arg, %rdi=func, %rdx=stack
        .type stack_trampoline, @function
	.cfi_startproc simple
	.cfi_def_cfa %rsp, 0
	# align the stack at 16 bytes for sse
	andq $-16, %rdx
	sub $8, %rdx
	mov %rsp, -8(%rdx)
	lea -8(%rdx), %rsp
	.cfi_rel_offset %rsp, 0
	call *%rsi
	pop %rsp
	.cfi_restore %rsp
	ret
	.cfi_endproc
