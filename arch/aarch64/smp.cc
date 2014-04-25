/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.h>
#include <osv/sched.hh>
#include <osv/prio.hh>
#include <osv/printf.hh>

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
    c->arch.smp_idx = 0;
    sched::cpus.push_back(c);
    sched::current_cpu = sched::cpus[0];
}

void smp_launch()
{
    auto boot_cpu = smp_initial_find_current_cpu();
    for (auto c : sched::cpus) {
        auto name = osv::sprintf("balancer%d", c->id);
        if (c == boot_cpu) {
            sched::thread::current()->_detached_state->_cpu = c;
            // c->init_on_cpu() already done in main().
            (new sched::thread([c] { c->load_balance(); },
                    sched::thread::attr().pin(c).name(name)))->start();
            c->init_idle_thread();
            c->idle_thread->start();
            continue;
        } else {
            abort();
        }
    }
}
