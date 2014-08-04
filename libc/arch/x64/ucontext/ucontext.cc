#include <api/ucontext.h>
#include <osv/stubbing.hh>
#include <stdint.h>

extern "C" { void start_context(void); }

void makecontext(struct __ucontext *ucp, void (*func)(void), int argc, ...)
{

    greg_t *sp;
    unsigned int idx_uc_link;
    va_list ap;
    int i;

    /* Generate room on stack for parameter if needed and uc_link. */
    sp = (greg_t *) ((uintptr_t) ucp->uc_stack.ss_sp + ucp->uc_stack.ss_size);
    sp -= (argc > 6 ? argc - 6 : 0) + 1;
    /* Align stack and make space for trampoline address. */
    sp = (greg_t *) ((((uintptr_t) sp) & -16L) - 8);

    idx_uc_link = (argc > 6 ? argc - 6 : 0) + 1;

    /* Setup context ucp. */
    /* Address to jump to. */
    ucp->uc_mcontext.gregs[REG_RIP] = (uintptr_t) func;
    /* Setup rbx.*/
    ucp->uc_mcontext.gregs[REG_RBX] = (uintptr_t) & sp[idx_uc_link];
    ucp->uc_mcontext.gregs[REG_RSP] = (uintptr_t) sp;

    /* Setup stack. */
    sp[0] = (uintptr_t) & start_context;
    sp[idx_uc_link] = (uintptr_t) ucp->uc_link;

    va_start(ap, argc);
    /* Handle arguments.

     The standard says the parameters must all be int values. This is
     an historic accident and would be done differently today. For
     x86-64 all integer values are passed as 64-bit values and
     therefore extending the API to copy 64-bit values instead of
     32-bit ints makes sense. It does not break existing
     functionality and it does not violate the standard which says
     that passing non-int values means undefined behavior. */
    for (i = 0; i < argc; ++i)
        switch (i) {
        case 0:
            ucp->uc_mcontext.gregs[REG_RDI] = va_arg(ap, greg_t);
            break;
        case 1:
            ucp->uc_mcontext.gregs[REG_RSI] = va_arg(ap, greg_t);
            break;
        case 2:
            ucp->uc_mcontext.gregs[REG_RDX] = va_arg(ap, greg_t);
            break;
        case 3:
            ucp->uc_mcontext.gregs[REG_RCX] = va_arg(ap, greg_t);
            break;
        case 4:
            ucp->uc_mcontext.gregs[REG_R8] = va_arg(ap, greg_t);
            break;
        case 5:
            ucp->uc_mcontext.gregs[REG_R9] = va_arg(ap, greg_t);
            break;
        default:
            /* Put value on stack. */
            sp[i - 5] = va_arg(ap, greg_t);
            break;
        }
    va_end(ap);
}

int swapcontext(struct __ucontext *oucp, const struct __ucontext *ucp)
{
    int ret;
    ret = getcontext(oucp);
    if (ret) {
        return ret;
    }
    return setcontext(ucp);
}
