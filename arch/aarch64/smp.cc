/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <smp.hh>
#include <osv/debug.h>
#include <osv/sched.hh>
#include <osv/prio.hh>
#include <osv/printf.hh>
#include <osv/aligned_new.hh>
#include <osv/export.h>
#include "processor.hh"
#include "psci.hh"
#include "arch-dtb.hh"
#include <alloca.h>

extern "C" { /* see boot.S */
    extern init_stack *smp_stack_free;
    extern u64 start_secondary_cpu();
}

OSV_LIBSOLARIS_API
volatile unsigned smp_processors = 1;

sched::cpu* smp_initial_find_current_cpu()
{
    for (auto c : sched::cpus) {
        if (c->arch.mpid == processor::read_mpidr())
            return c;
    }
    abort();
}

void secondary_bringup(sched::cpu* c)
{
    __sync_fetch_and_add(&smp_processors, 1);
    c->idle_thread->start();
    c->load_balance();
}

void smp_init()
{
    int nr_cpus = dtb_get_cpus_count();
    if (nr_cpus < 1) {
        abort("smp_init: could not get cpus from device tree.\n");
    }
    debug(fmt("%d CPUs detected\n") % nr_cpus);
    u64 *mpids = (u64 *)alloca(sizeof(u64) * nr_cpus);
    if (!dtb_get_cpus_mpid(mpids, nr_cpus)) {
        abort("smp_init: failed to get cpus mpids from device tree.\n");
    }

    for (int i = 0; i < nr_cpus; i++) {
        auto c = new sched::cpu(i);
        c->arch.mpid = mpids[i];
        c->arch.smp_idx = i;
        c->arch.initstack.next = smp_stack_free;  /* setup thread stack */
        smp_stack_free = &c->arch.initstack;
        sched::cpus.push_back(c);
    }
    sched::current_cpu = sched::cpus[0];

    for (auto c : sched::cpus) {
        c->incoming_wakeups = aligned_array_new<sched::cpu::incoming_wakeup_queue>(sched::cpus.size());
    }
}

void smp_launch()
{
#if CONF_logger_debug
    debug_early_entry("smp_launch");
#endif
    for (auto c : sched::cpus) {
        auto name = osv::sprintf("balancer%d", c->id);
        if (c->arch.smp_idx == 0) {
            sched::thread::current()->_detached_state->_cpu = c;
            // c->init_on_cpu() already done in main().
            (new sched::thread([c] { c->load_balance(); },
                    sched::thread::attr().pin(c).name(name)))->start();
            c->init_idle_thread();
            c->idle_thread->start();
            continue;
        }
        sched::thread::attr attr;
        attr.stack(81920).pin(c).name(name);
        c->init_idle_thread();
        c->bringup_thread = new sched::thread([=] { secondary_bringup(c); }, attr, true);
        psci::_psci.cpu_on(c->arch.mpid, mmu::virt_to_phys(reinterpret_cast<void *>(start_secondary_cpu)));
    }
    while (smp_processors != sched::cpus.size())
        barrier();
}

void smp_main()
{
#if CONF_logger_debug
    debug_early_entry("smp_main");
#endif
    sched::cpu* cpu = smp_initial_find_current_cpu();
    assert(cpu);
    cpu->init_on_cpu();
    cpu->bringup_thread->_detached_state->_cpu = cpu;
    cpu->bringup_thread->switch_to_first();
}

static inter_processor_interrupt smp_stop_cpu_ipi { IPI_SMP_STOP, [] {
    while (true) {
        arch::halt_no_interrupts();
    }
}};

void smp_crash_other_processors()
{
    smp_stop_cpu_ipi.send_allbutself();
}
