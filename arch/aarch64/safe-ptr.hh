/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SAFE_PTR_HH_
#define SAFE_PTR_HH_

#include <osv/compiler.h>

template <typename T>
static inline bool
safe_load(const T* potentially_bad_ptr, T& data)
{
    unsigned long long reg = 0;
    unsigned char ok = true;
    asm ("1: \n"
         "ldr %[reg], %[ptr] \n"
         "str %[reg], %[data] \n"
         "2: \n"
         ".pushsection .text.fixup, \"ax\" \n"
         "3: \n"
         "mov %[ok], #0 \n"
         "b 2b \n"
         ".popsection \n"
         ".pushsection .fixup, \"aw\" \n"
         ".quad 1b, 3b \n"
         ".popsection\n"
         : [reg]"+&r"(reg), [data]"=Q"(data), [ok]"=r"(ok)
         : [ptr]"Q"(*potentially_bad_ptr)
         : "memory");
    return ok;
}

template <typename T>
static inline bool
safe_store(const T* potentially_bad_ptr, const T& data)
{
    unsigned long long reg = 0;
    asm goto ("1: \n"
              "ldr %[reg], %[data] \n"
              "str %[reg], %[ptr] \n"
              ".pushsection .fixup, \"aw\" \n"
              ".quad 1b, %l[fail] \n"
              ".popsection\n"
              :
              : [reg]"r"(reg), [ptr]"Q"(*potentially_bad_ptr), [data]"Q"(data)
              : "memory"
              : fail);
     return true;
 fail: ATTR_COLD_LABEL;
     return false;
}

#endif /* SAFE_PTR_HH_ */
