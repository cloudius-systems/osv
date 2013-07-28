#ifndef EXECINFO_HH_
#define EXECINFO_HH_

// Similar to backtrace(), but works even with a corrupted stack.  Uses
// frame pointers instead of DWARF debug information, so it works in interrupt
// contexts, but requires -fno-omit-frame-pointer
int backtrace_safe(void** pc, int nr);


#endif /* EXECINFO_HH_ */
