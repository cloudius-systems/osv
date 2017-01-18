/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef SMP_HH_
#define SMP_HH_

#include <osv/sched.hh>
#include <osv/prio.hh>

void smp_init() __attribute__((constructor(init_prio::sched)));
void smp_launch();
sched::cpu* smp_initial_find_current_cpu();
void smp_crash_other_processors();

#endif /* SMP_HH_ */
