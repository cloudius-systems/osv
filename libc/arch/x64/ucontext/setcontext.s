.extern abort
.extern sigprocmask
.global __setcontext
.global _setcontext
.global setcontext
.type __setcontext,@function
.type _setcontext,@function
.type setcontext,@function
__setcontext:
_setcontext:
setcontext:
	/* Save %rdi on stack */
	pushq	%rdi

	/* Restore the signal mask with
	 * syscall sigprocmask (SIG_SETMASK = 2, mask, NULL).
	 * Ordinary call to function, because the instruction
	 * syscall generates invalid opcode exception.
	 * */
	leaq 280(%rdi), %rsi
	movq $2, %rdi
	xorq %rdx, %rdx
	movq $8,%rcx
	call sigprocmask

	/* Reload %rdi, adjust stack. */
	popq %rdi
		/* Check %rax for error and call abort.
	 * Commented because sigprocmask does not
	 * implement error checking.
	 * cmpq $-4095, %rax
	 * jae abort
	 **/

	/* Restore the floating-point context. Not the registers, only the
	rest. */
	movq 208(%rdi), %rcx
	fldenv (%rcx)
	ldmxcsr 432(%rdi)


	/* Load the new stack pointer, the preserved registers and
	registers used for passing args. */
	movq 160(%rdi), %rsp
	movq 128(%rdi), %rbx
	movq 120(%rdi), %rbp
	movq 72(%rdi), %r12
	movq 80(%rdi), %r13
	movq 88(%rdi), %r14
	movq 96(%rdi), %r15

	/* The following ret should return to the address set with
	getcontext. Therefore push the address on the stack. */
	movq 168(%rdi), %rcx
	pushq %rcx

	movq 112(%rdi), %rsi
	movq 136(%rdi), %rdx
	movq 152(%rdi), %rcx
	movq 40(%rdi), %r8
	movq 48(%rdi), %r9

	/* Setup finally %rdi. */
	movq 104(%rdi), %rdi


	/* Clear rax to indicate success. */
	xorq %rax, %rax
	ret
