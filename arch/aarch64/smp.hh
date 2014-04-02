/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_SMP_HH
#define ARCH_SMP_HH

#include <osv/sched.hh>

sched::cpu* smp_initial_find_current_cpu();

#endif /* ARCH_SMP_HH_ */
