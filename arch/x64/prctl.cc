/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch.hh"
#include "msr.hh"
#include "libc/libc.hh"

#include <assert.h>
#include <stdio.h>

enum {
    ARCH_SET_GS = 0x1001,
    ARCH_SET_FS = 0x1002,
    ARCH_GET_FS = 0x1003,
    ARCH_GET_GS = 0x1004,
};

long arch_prctl(int code, unsigned long addr)
{
    switch (code) {
    case ARCH_SET_GS:
        processor::wrmsr(msr::IA32_GS_BASE, addr);
        asm volatile ("movq %0, %%gs" :: "r"(addr));
        break;
    case ARCH_SET_FS:
        processor::wrmsr(msr::IA32_FS_BASE, addr);
        asm volatile ("movq %0, %%fs" :: "r"(addr));
        break;
    case ARCH_GET_FS:
        return processor::rdmsr(msr::IA32_FS_BASE);
    case ARCH_GET_GS:
        return processor::rdmsr(msr::IA32_GS_BASE);
    }
    return libc_error(EINVAL);
}
