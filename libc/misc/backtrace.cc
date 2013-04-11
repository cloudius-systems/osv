#include <execinfo.h>

#if 0
// Implementation using libunwind.a. Unfortunately, despite linking with
// libwind.a I couldn't get this to link, because of some symbol
// dependency hell.
static int backtrace(void **buffer, int size) {
    unw_context_t context;
    unw_getcontext(&context);
    unw_cursor_t cursor;
    unw_init_local(&cursor, &context);

    int count = 0;
    while (count < size && unw_step(&cursor)) {
        unw_word_t ip;
        unw_get_reg (&cursor, UNW_REG_IP, &ip);
        buffer[count++] = (void*) ip;
    }
    return count;
}
#endif

// An alternative, slightly more awkward but functioning implementation,
// using the _Unwind* functions which are used by the GCC runtime and
// supplied in libgcc_s.so). These also use libunwind.a internally, I believe.
#include <unwind.h>
struct worker_arg {
    void **buffer;
    int size;
    int pos;
    unsigned long prevcfa;
};

static _Unwind_Reason_Code worker (struct _Unwind_Context *ctx, void *data)
{
    struct worker_arg *arg = (struct worker_arg *) data;
    if (arg->pos >= 0) {
        arg->buffer[arg->pos] = (void *)_Unwind_GetIP(ctx);
        unsigned long cfa = _Unwind_GetCFA(ctx);
        if (arg->pos > 0 && arg->buffer[arg->pos-1] == arg->buffer[arg->pos]
                         && arg->prevcfa == cfa) {
            return _URC_END_OF_STACK;
        }
        arg->prevcfa = cfa;
    }
    if (++arg->pos == arg->size) {
        return _URC_END_OF_STACK;
    }
    return _URC_NO_REASON;
}

int backtrace(void **buffer, int size)
{
    // we start with arg.pos = -1 to skip the first frame (since backtrace()
    // is not supposed to include the backtrace() function itself).
    struct worker_arg arg { buffer, size, -1, 0 };
    _Unwind_Backtrace(worker, &arg);
    return arg.pos > 0 ? arg.pos : 0;
}
