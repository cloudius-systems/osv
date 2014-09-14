/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/execinfo.h>

#if 0
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
#endif

#if 1
// An alternative, slightly more awkward but functioning implementation,
// using the _Unwind* functions which are used by the GCC runtime and
// supplied in libgcc_s.so or libgcc_eh.a).
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
    // _Unwind_Backtrace seems to put a null pointer as the top-most caller,
    // which is of no interest to us.
    if (arg.pos > 0 && buffer[arg.pos-1] == nullptr) {
        arg.pos--;
    }
    return arg.pos > 0 ? arg.pos : 0;
}
#endif

#if 0
// This is the third implementation of backtrace(), using gcc's builtins
//__builtin_frame_address and __builtin_return_address. This is the ugliest
// of the three implementation, because these builtins awkwardly require
// constant arguments, so instead of a simple loop, we needed to resort
// to ugly C-preprocessor hacks. This implementation also requires
// compilation without -fomit-frame-pointer.
// The good thing about this implementation is that the gcc builtin
// interface gives us a chance to ignore suspicious frame addresses before
// continuing to investigate them. See backtrace_safe() for a better
// implementation of this idea, with safe loads that can never crash.
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

#include <osv/demangle.hh>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

void backtrace_symbols_fd(void *const *addrs, int len, int fd)
{
    for (int i = 0; i < len; i++) {
        char name[1024];
        // FIXME: I think Linux also shows the name of the object
        osv::lookup_name_demangled(addrs[i], name, sizeof(name));
        auto remain = strlen(name);
        snprintf(name + remain, sizeof(name) - remain, " [%p]\n", addrs[i]);
        remain = strlen(name);
        while (remain > 0) {
            auto n = write(fd, name, remain);
            if (n < 0) {
                return; // write error, nothing better we can do...
            }
            remain -= n;
        }
    }
}
char **backtrace_symbols(void *const *addrs, int len)
{
    // We need to return a newly allocated char **, i.e., an array of len
    // pointers. We put the strings we point to after this array, allocated
    // together, so when the user free()s the array, the strings will also
    // be freed.
    size_t used = len * sizeof(char*);
    size_t bufsize = used + 1;
    char *buf = (char*)malloc(bufsize);
    char **ret = (char **)buf;
    for (int i = 0; i < len; i++) {
        char name[1024];
        // FIXME: I think Linux also shows the name of the object
        osv::lookup_name_demangled(addrs[i], name, sizeof(name));
        auto remain = strlen(name);
        snprintf(name + remain, sizeof(name) - remain, " [%p]", addrs[i]);
        remain = strlen(name);
        if (remain >= (bufsize - used)) {
            bufsize = bufsize*2 + remain + 1;
            buf = (char*)realloc(buf, bufsize);
            ret = (char **)buf;
        }
        ret[i] = buf + used;
        strcpy(buf + used, name);
        used += remain + 1;
    }
    return ret;
}
