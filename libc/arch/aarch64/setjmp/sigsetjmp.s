.global sigsetjmp
.global __sigsetjmp
.type sigsetjmp,%function
.type __sigsetjmp,%function
sigsetjmp:
__sigsetjmp:
	str x1,[x0,#176]
	cbz x1,setjmp

	// TODO errno?
	// sigprocmask(SIG_SETMASK, 0, (sigset_t*)buf->__ss);
	stp x0,x30,[sp,#-16]!
	add x2,x0,#184
	mov x1,#0
	mov x0,#2
	bl sigprocmask
	ldp x0,x30,[sp],#16

	b setjmp
