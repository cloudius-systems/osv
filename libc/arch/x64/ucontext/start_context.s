.extern setcontext
.extern abort
.extern exit
.global __start_context
.global _start_context
.global start_context
.type __start_context,@function
.type _start_context,@function
.type start_context,@function
__start_context:
_start_context:
start_context:
	/* This removes the parameters passed to the function given to
	'makecontext' from the stack. RBX contains the address
	on the stack pointer for the next context. */
	movq	%rbx, %rsp

	popq %rdi /* This is the next context. */
	testq %rdi, %rdi
	je 2f	/* If it is zero exit. */

	call setcontext
	/* If this returns (which can happen if the syscall fails) we'll
	exit the program with the return error value (-1). */
	movq %rax,%rdi

	2:
	call exit
	/* The 'exit' call should never return. In case it does cause
	the process to terminate. */
	call abort
