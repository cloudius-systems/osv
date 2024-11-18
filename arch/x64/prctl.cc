/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch.hh"
#include "libc/libc.hh"

#include <assert.h>
#include <stdio.h>

#include <osv/sched.hh>

enum {
    ARCH_SET_GS = 0x1001,
    ARCH_SET_FS = 0x1002,
    ARCH_GET_FS = 0x1003,
    ARCH_GET_GS = 0x1004,
};

long arch_prctl(int code, unsigned long addr)
{
    switch (code) {
    case ARCH_SET_FS:
        sched::thread::current()->set_app_tcb(addr);
        return 0;
    case ARCH_GET_FS:
        return sched::thread::current()->get_app_tcb();
    }
    return libc_error(EINVAL);
}
