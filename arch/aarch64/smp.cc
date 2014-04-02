/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.h>
#include <osv/sched.hh>
#include <osv/prio.hh>

volatile unsigned smp_processors = 1;

sched::cpu* smp_initial_find_current_cpu()
{
    for (auto c : sched::cpus) {
        /* just return the single cpu we have for now */
        return c;
    }
    abort();
}

void __attribute__((constructor(init_prio::sched))) smp_init()
{
    auto c = new sched::cpu(0);
    c->arch.gic_id = 0;
    sched::cpus.push_back(c);
    sched::current_cpu = sched::cpus[0];
}
