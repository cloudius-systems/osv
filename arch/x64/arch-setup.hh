/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_SETUP_HH_
#define ARCH_SETUP_HH_

#include <osv/tls.hh>

void arch_init_premain();
void arch_setup_tls(thread_control_block *tcb);

void arch_setup_free_memory();

#endif /* ARCH_SETUP_HH_ */
