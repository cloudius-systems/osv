/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SAFE_PTR_HH_
#define SAFE_PTR_HH_

#include <osv/compiler.h>

template <typename T>
[[gnu::always_inline]] // if not inline, ld can discard a duplicate function
static inline bool
safe_load(const T* potentially_bad_pointer, T& data)
{
    unsigned char ok = true;
    // would like to use asm goto, but it doesn't support output operands.
    asm
       ("1: \n\t"
        "mov %[ptr], %[data] \n\t"
        "2: \n\t"
        ".pushsection .text.fixup, \"ax\" \n\t"
        "3: \n\t"
        "movb $0, %[ok] \n\t"
        "jmp 2b \n\t"
        ".popsection \n\t"
        ".pushsection .fixup, \"a\" \n\t"
        ".quad 1b, 3b \n\t"
        ".popsection\n"
            : [data]"=r"(data), [ok]"+rm"(ok)
            : [ptr]"m"(*potentially_bad_pointer));
    return ok;
}

template <typename T>
[[gnu::always_inline]] // if not inline, ld can discard a duplicate function
static inline bool
safe_store(const T* potentially_bad_pointer, const T& data)
{
    asm goto
       ("1: \n\t"
        "mov %[data], %[ptr] \n\t"
        ".pushsection .fixup, \"a\" \n\t"
        ".quad 1b, %l[fail] \n\t"
        ".popsection\n"
            :
            : [ptr]"m"(*potentially_bad_pointer), [data]"r"(data)
            : "memory"
            : fail);
    return true;
    fail: ATTR_COLD_LABEL;
    return false;
}


#endif /* SAFE_PTR_HH_ */
