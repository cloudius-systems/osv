/* Copyright 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "syscall.h"
#include <signal.h>

static const unsigned long all_mask[] = { -1UL };

static const unsigned long app_mask[] = {
	0xfffffffc7fffffffUL
};

void __block_all_sigs(void *set)
{
    sigprocmask(SIG_BLOCK, (void*)&all_mask, set);
}

void __block_app_sigs(void *set)
{
    sigprocmask(SIG_BLOCK, (void*)&app_mask, set);
}

void __restore_sigs(void *set)
{
    sigprocmask(SIG_SETMASK, set, 0);
}
