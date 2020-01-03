#ifndef OSV_VMFUNC_HH_
#define OSV_VMFUNC_HH_

extern "C" long trampoline(void);

// rdi, rsi, rdx, rcx, r8, r9
long vmfunc(long p1, long p2, long p3, long n, long p4, long p5);

#endif