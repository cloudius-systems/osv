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

.align 16
.global syscall_entry
syscall_entry:
        .type syscall_entry, @function
        .cfi_startproc simple
	# There is no ring transition and rflags are left unchanged.

    # We need to save and restore the caller's %rbp anyway, so let's also
    # set it up properly for old-style frame-pointer backtracing to work
    # (e.g., backtrace_safe()). Also need to push the return address before
    # the rbp to get a normal frame. Our return address is in rcx.
    pushq %rcx
    pushq %rbp
    movq %rsp, %rbp
	#
	# From http://stackoverflow.com/questions/2535989/what-are-the-calling-conventions-for-unix-linux-system-calls-on-x86-64:
	# "User-level applications use as integer registers for passing the sequence %rdi, %rsi, %rdx, %rcx, %r8 and %r9. The kernel interface uses %rdi, %rsi, %rdx, %r10, %r8 and %r9"

        # FIXME: fpu
	# build the stack frame by hand
	pushq %rsp
	subq $8, %rsp # rip was saved in rcx by the syscall instruction
	pushq %rax
	pushq %rbx
	pushq %rcx # contains rip before syscall instruction
	pushq %rdx
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11 # contains rflags before syscall instruction
	pushq %r12
	pushq %r13
	pushq %r14
	pushq %r15

        # stack contains a signal_frame
        .cfi_signal_frame
        .cfi_def_cfa %rsp, 0
	.cfi_register rip,rcx # rcx took previous rip value
	.cfi_register rflags,r11 # r11 took previous rflags value
	.cfi_undefined rcx # was overwritten with rip by the syscall instruction
	.cfi_undefined r11 # was overwritten with rflags by the syscall instruction
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

	# The kernel interface use r10 as fourth argument while the user interface use rcx
	# so overwrite rcx with r10
	movq %r10, %rcx

	# prepare function call parameter: r9 is on the stack since it's the seventh param
	# because we shift existing params by one to make room for syscall number
	pushq %r9
	movq %r8, %r9
	movq %rcx, %r8
	movq %rdx, %rcx
	movq %rsi, %rdx
	movq %rdi, %rsi
	# syscall number from rax as first argument
	movq %rax, %rdi

    # align stack to 16 bytes, as required by the ABI.
    # Counting the pushes above is not enough because we don't know what was
    # the stack alignment initially (syscall is not a function call so it can
    # be called with any stack alignment). An additional complication is that
    # we need to restore %rsp later without knowing how it was previously
    # aligned. In the following trick, not using an additional register, the
    # two pushes leave the stack with the same alignment it had originally,
    # and a copy of the original %rsp at (%rsp) and 8(%rsp). The andq then
    # aligns the stack - if it was already 16 byte aligned nothing changes, if
    # it was 8 byte aligned then it subtracts 8 from %rsp, meaning that the
    # original %rsp is now at 8(%rsp) and 16(%rsp). In both cases we can
    # restore it from 8(%rsp).
    pushq %rsp
    pushq (%rsp)
    andq $-0x10, %rsp

	callq syscall_wrapper

    movq 8(%rsp), %rsp

	popq %r9
	# in Linux user and kernel return value are in rax so we have nothing to do for return values

	popq %r15
	popq %r14
	popq %r13
	popq %r12
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rdx
	popq %rcx
	popq %rbx
        addq $8, %rsp  # skip rax emplacement (return value is in rax)
        addq $8, %rsp  # rip emplacement (rip cannot be popped)
	popq %rsp

    popq %rbp
    popq %rcx

	# jump to rcx where the syscall instruction put rip
	# (sysret would leave rxc cloberred so we have nothing to do to restore it)
	jmpq *%rcx
	.cfi_endproc
.size syscall_entry, .-syscall_entry
