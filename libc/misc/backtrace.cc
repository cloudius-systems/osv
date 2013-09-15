/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <execinfo.h>

// Implementation using libunwind.a. This implementation works even with code
// compiled with omit-frame-pointer.
#define UNW_LOCAL_ONLY
#include <libunwind.h>
int backtrace(void **buffer, int size) {
    unw_context_t context;
    if (unw_getcontext(&context) < 0)
        return 0;
    unw_cursor_t cursor;
    if (unw_init_local(&cursor, &context) < 0)
        return 0;

    int count = 0;
    while (count < size && unw_step(&cursor) > 0) {
        unw_word_t ip;
        unw_get_reg (&cursor, UNW_REG_IP, &ip);
        buffer[count++] = (void*) ip;
    }
    return count;
}

#if 0
// An alternative, slightly more awkward but functioning implementation,
// using the _Unwind* functions which are used by the GCC runtime and
// supplied in libgcc_s.so). These also use libunwind.a internally, I believe.
//
// Unfortunately, while this implementation works nicely on our own code,
// it fails miserably when java.so is running. Supposedly it sees some
// unexpected frame pointers, but rather than let us see them and deal
// with them in worker(), it fails in _Unwind_Backtrace before calling
// worker(), I don't know why. So stay tuned for the third implementation,
// below.
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
#endif

#if 0
// This is the third implementation of backtrace(), using gcc's builtins
//__builtin_frame_address and __builtin_return_address. This is the ugliest
// of the three implementation, because these builtins awkwardly require
// constant arguments, so instead of a simple loop, we needed to resort
// to ugly C-preprocessor hacks. This implementation also requires
// compilation without -fomit-frame-pointer (currently our "release"
// build is compiled with it, so use our "debug" build).
//
// The good thing about this implementation is that the gcc builtin
// interface gives us a chance to ignore suspicious frame addresses before
// continuing to investigate them - and thus allows us to also backtrace
// when running Java code - some of it running from the heap and, evidently,
// contains broken frame information.
int backtrace(void **buffer, int size)
{
    void *fa, *ra;
#define sanepointer(x) ((unsigned long)(x)<<16>>16 == (unsigned long)(x))
#define TRY(i) \
    if (i>=size) \
        return size; \
    fa = __builtin_frame_address(i); \
    if (!fa || (unsigned long)fa < 0x200000000000UL || !sanepointer(fa)) \
        return i; \
    ra = __builtin_return_address(i); \
    if ((unsigned long)ra < 4096UL || (unsigned long)ra > 0x200000000000UL) \
        return i; \
    buffer[i] = ra;
#define TRY7(i) TRY(i+0) TRY(i+1) TRY(i+2) TRY(i+3) TRY(i+4) TRY(i+5) TRY(i+6)
    TRY7(0) TRY7(7) TRY7(14) TRY7(21)   // get up to 28 levels of calls
    return 28;
}
#endif
