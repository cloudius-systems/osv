/* Copyright 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <setjmp.h>

extern void __restore_sigs(void *set);

_Noreturn void siglongjmp(sigjmp_buf buf, int ret)
{
    while (1) {
        asm volatile ("wfi");
    }
}
