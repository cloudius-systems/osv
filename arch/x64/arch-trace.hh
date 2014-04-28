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
    trace_slow_path(assign(as...));
}

#endif /* ARCH_TRACE_HH_ */
