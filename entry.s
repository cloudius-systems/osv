
.macro exception_entry name, handler, has_error_code
	.global \name
	\name :
	.cfi_startproc simple
	.cfi_signal_frame
	.if \has_error_code == 0
	pushq $0
	.endif
	.cfi_def_cfa rsp, 0
	.cfi_rel_offset rip, 8
	.cfi_rel_offset rsp, 32
	pushq %rax
	.cfi_adjust_cfa_offset 8
	pushq %rbx
	.cfi_adjust_cfa_offset 8
	pushq %rcx
	.cfi_adjust_cfa_offset 8
	pushq %rdx
	.cfi_adjust_cfa_offset 8
	pushq %rsi
	.cfi_adjust_cfa_offset 8
	pushq %rdi
	.cfi_adjust_cfa_offset 8
	pushq %rbp
	.cfi_adjust_cfa_offset 8
	pushq %r8
	.cfi_adjust_cfa_offset 8
	pushq %r9
	.cfi_adjust_cfa_offset 8
	pushq %r10
	.cfi_adjust_cfa_offset 8
	pushq %r11
	.cfi_adjust_cfa_offset 8
	pushq %r12
	.cfi_adjust_cfa_offset 8
	pushq %r13
	.cfi_adjust_cfa_offset 8
	pushq %r14
	.cfi_adjust_cfa_offset 8
	pushq %r15
	.cfi_adjust_cfa_offset 8
	mov %rsp, %rdi
	call \handler
	popq %r15
	.cfi_adjust_cfa_offset -8
	popq %r14
	.cfi_adjust_cfa_offset -8
	popq %r13
	.cfi_adjust_cfa_offset -8
	popq %r12
	.cfi_adjust_cfa_offset -8
	popq %r11
	.cfi_adjust_cfa_offset -8
	popq %r10
	.cfi_adjust_cfa_offset -8
	popq %r9
	.cfi_adjust_cfa_offset -8
	popq %r8
	.cfi_adjust_cfa_offset -8
	popq %rbp
	.cfi_adjust_cfa_offset -8
	popq %rdi
	.cfi_adjust_cfa_offset -8
	popq %rsi
	.cfi_adjust_cfa_offset -8
	popq %rdx
	.cfi_adjust_cfa_offset -8
	popq %rcx
	.cfi_adjust_cfa_offset -8
	popq %rbx
	.cfi_adjust_cfa_offset -8
	popq %rax
	.cfi_adjust_cfa_offset -8
	iretq
	.cfi_endproc
.endm

.macro exception_error_entry name, handler
	exception_entry \name, \handler, 1
.endm

.macro exception_noerror_entry name, handler
	exception_entry \name, \handler, 0
.endm

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
