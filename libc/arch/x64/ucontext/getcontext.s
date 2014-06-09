.extern abort
.extern sigprocmask
.global __getcontext
.global _getcontext
.global getcontext
.type __getcontext,@function
.type _getcontext,@function
.type getcontext,@function
__getcontext:
_getcontext:
getcontext:

	/* Save the all registers.*/
	movq %rbx, 128(%rdi)
	movq %rbp, 120(%rdi)
	movq %r12, 72(%rdi)
	movq %r13, 80(%rdi)
	movq %r14, 88(%rdi)
	movq %r15, 96(%rdi)

	movq %rdi, 104(%rdi)
	movq %rsi, 112(%rdi)
	movq %rdx, 136(%rdi)
	movq %rcx, 152(%rdi)
	movq %r8, 40(%rdi)
	movq %r9, 48(%rdi)

	movq (%rsp), %rcx
	movq %rcx, 168(%rdi)
	leaq 8(%rsp), %rcx
	movq %rcx, 160(%rdi)

	movq %rbx, 128(%rdi)
	movq %rbp, 120(%rdi)
	movq %r12, 72(%rdi)
	movq %r13, 80(%rdi)
	movq %r14, 88(%rdi)
	movq %r15, 96(%rdi)

	movq %rdi, 104(%rdi)
	movq %rsi, 112(%rdi)
	movq %rdx, 136(%rdi)
	movq %rcx, 152(%rdi)
	movq %r8, 40(%rdi)
	movq %r9, 48(%rdi)

	movq (%rsp), %rcx
	movq %rcx, 168(%rdi)
	leaq 8(%rsp), %rcx
	movq %rcx, 160(%rdi)

	leaq 408(%rdi), %rcx
	movq %rcx, 208(%rdi)

	/* Save the floating-point environment. */
	fnstenv (%rcx)
	fldenv (%rcx)
	stmxcsr 432(%rdi)

	/* Save the current signal mask with
	 * sigprocmask (SIG_BLOCK = 0, NULL, mask).
	 * Ordinary call to function, because the instruction
	 * syscall generates invalid opcode exception.
	 * */
	leaq 280(%rdi), %rdx
	xorq %rdi, %rdi
	xorq %rsi, %rsi
	movq $8,%rcx
	call sigprocmask
	/* Check %rax for error and call abort.
	 * Commented because sigprocmask does not
	 * implement error checking.
	 * cmpq $-4095, %rax
	 * jae abort
	 **/

	/* All done, return 0 for success. */
	xorq %rax, %rax
	ret
