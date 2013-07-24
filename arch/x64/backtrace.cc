#include "safe-ptr.hh"

#include <osv/execinfo.hh>

struct frame {
    frame* next;
    void* pc;
};

int backtrace_safe(void** pc, int nr)
{
    frame* rbp;
    frame* next;

    asm("mov %%rbp, %0" : "=rm"(rbp));
    int i = 0;
    while (i < nr
            && safe_load(&rbp->next, next)
            && safe_load(&rbp->pc, pc[i])
            && pc[i]) {
        rbp = next;
        ++i;
    }
    return i;
}



