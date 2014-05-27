# Copyright (C) 2013 Cloudius Systems, Ltd.
#
# This work is open source software, licensed under the terms of the
# BSD license as described in the LICENSE file in the top-level directory.


#include "cfi.S"

.text

.global __elf_resolve_pltgot

// calling convension:
// %rsp + 8: index
// %rsp + 0:  object
__elf_resolve_pltgot:
        .type __elf_resolve_pltgot, @function
	.cfi_startproc simple
	.cfi_def_cfa %rsp, 0
	.cfi_rel_offset %rip, 16
	sub $8, %rsp  # make room for resolved address
	.cfi_adjust_cfa_offset 8
	pushq_cfi %rbp
	.cfi_adjust_cfa_offset 8
	mov %rsp, %rbp
	.cfi_def_cfa %rbp, 16

	# align stack
	andq $-16, %rsp

	pushq_cfi %rax
	pushq_cfi %rbx
	pushq_cfi %rcx
	pushq_cfi %rdx
	pushq_cfi %rsi
	pushq_cfi %rdi
	pushq_cfi %r8
	pushq_cfi %r9
	pushq_cfi %r10
	pushq_cfi %r11
	pushq_cfi %r12
	pushq_cfi %r13
	pushq_cfi %r14
	pushq_cfi %r15
	mov 24(%rbp), %edi
	mov 16(%rbp), %rsi
	call elf_resolve_pltgot
	mov %rax, 8(%rbp)
	popq_cfi %r15
	popq_cfi %r14
	popq_cfi %r13
	popq_cfi %r12
	popq_cfi %r11
	popq_cfi %r10
	popq_cfi %r9
	popq_cfi %r8
	popq_cfi %rdi
	popq_cfi %rsi
	popq_cfi %rdx
	popq_cfi %rcx
	popq_cfi %rbx
	popq_cfi %rax
	leave
	ret $16
	.cfi_endproc
