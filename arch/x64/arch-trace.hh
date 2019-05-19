/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_TRACE_HH_
#define ARCH_TRACE_HH_

#include "osv/trace.hh"

template<unsigned _id, typename ... s_args, typename ... r_args, std::tuple<
        s_args...> (*assign)(r_args...)>
inline void tracepointv<_id, std::tuple<s_args...>(r_args...), assign>::operator()(
        r_args ... as) {
    // See comment in cc file about why this is force-aligned
    asm goto(".balign 2 \n\t"
            "1: .byte 0x0f, 0x1f, 0x44, 0x00, 0x00 \n\t"  // 5-byte nop
            ".pushsection .tracepoint_patch_sites, \"aw\", @progbits \n\t"
            ".quad %c[id] \n\t"
            ".quad %c[type] \n\t"
            ".quad 1b \n\t"
            ".quad %l[slow_path] \n\t"
            ".popsection"
            : : [type]"i"(&typeid(*this)), [id]"i"(_id) : : slow_path);
    return;
slow_path:
    // We don't want register shuffling and function calls here, so pretend
    // to the compiler that the slow path just stores some data into local
    // memory and executes an instruction that clobbers just a few registers
    // (instead of lots of registers and all of memory):
    auto data = assign(as...);
    auto pdata = &data;
    // encapsulate the trace_slow_path() in a function callable from asm:
    void (*do_slow_path)(tracepointv* tp, decltype(data)* d)
            = [](tracepointv* tp, decltype(data)* d) __attribute__((cold)) {
        tp->trace_slow_path(*d);
    };
    tracepointv* tp = this;
    // and call it, saving caller-saved registers:
    asm volatile(
            "sub $128, %%rsp \n\t" // avoid red zone
            "push %%rcx; push %%rdx; push %%r8; push %%r9; push %%r10; push %%r11 \n\t"
            "call *%%rax \n\t"
            "pop %%r11; pop %%r10; pop %%r9; pop %%r8; pop %%rdx; pop %%rcx \n\t"
            "add $128, %%rsp \n\t"
            : "+a"(do_slow_path), "+D"(tp), "+S"(pdata) : "m"(data)
            );
}

#endif /* ARCH_TRACE_HH_ */
