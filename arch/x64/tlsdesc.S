.text
.global __tlsdesc_static
.hidden __tlsdesc_static
.type __tlsdesc_static,@function
__tlsdesc_static:
	mov 8(%rax),%rax
	ret

// size_t __tlsdesc_dynamic(size_t *a)
//
// TLS descriptor resolver for thread-local variables of dlopen()-ed objects.
// On entry %rax points to the descriptor, whose second word (a[1]) points
// to a module_and_offset struct. Returns in %rax the offset of the variable
// from the thread pointer (%fs:0). Per the TLS descriptor calling convention,
// all other registers must be preserved.
.global __tlsdesc_dynamic
.hidden __tlsdesc_dynamic
.type __tlsdesc_dynamic,@function
__tlsdesc_dynamic:
//FAST PATH
	pushq %rcx
	pushq %rdx
	mov 8(%rax),%rax        // p - pointer to the module_and_offset struct
	mov (%rax),%rcx         // load p->module
	mov %fs:24,%rdx         // load the dtv address stored in thread_control_block (see arch-tls.hh)
	cmp (%rdx),%rcx         // compare p->module with last_index - 1st field in the dtv struct
	ja 1f                   // execute slow path to setup tls for p->module IF p->module > last_index
	mov 8(%rdx),%rdx        // load first - 2nd field in the dtv struct - address of 1st element of the _tls vector (see sched.hh)
	mov (%rdx,%rcx,8),%rdx  // load the address of this module tls block
	test %rdx,%rdx          // test if this module tls block has been setup for this thread
	jz 1f                   // execute slow path to setup tls for p->module IF it is null
	                        // --- at this point this module TLS block for this thread has been setup
2:	add 8(%rax),%rdx        // add p->offset to this module TLS block address
	sub %fs:0,%rdx          // subtract the thread pointer per TLS descriptor contract
	mov %rdx,%rax
	popq %rdx
	popq %rcx
	ret

//SLOW PATH
1:	pushq %rbp
	mov %rsp,%rbp
	andq $-16,%rsp          // align stack for the call

	//Save the remaining caller-saved registers (%rcx and %rdx were saved above)
	pushq %rax
	pushq %rsi
	pushq %rdi
	pushq %r8
	pushq %r9
	pushq %r10
	pushq %r11
	sub $8,%rsp             // keep the stack 16-byte aligned

	//The __tls_dynamic_setup will save all FPU registers as well
	mov %rax,%rdi
	call __tls_dynamic_setup
	mov %rax,%rdx           // put address of newly allocated TLS block into %rdx

	add $8,%rsp
	popq %r11
	popq %r10
	popq %r9
	popq %r8
	popq %rdi
	popq %rsi
	popq %rax
	leave
	jmp 2b
