#include <setjmp.h>

extern "C"
void __longjmp_chk (jmp_buf env, int val)
{
    // TODO: Glibc's __longjmp_chk additionally does some sanity checks about
    // whether we're jumping a sane stack frame.
    longjmp(env, val);
}
