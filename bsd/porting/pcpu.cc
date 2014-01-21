/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/sched.hh>
#include "osv/percpu.hh"
#include <bsd/porting/pcpu.h>
#include <osv/debug.hh>

PERCPU(struct pcpu, pcpu);

static void pcpu_porting_init()
{
    pcpu->pc_cpuid = sched::cpu::current()->id;
}
sched::cpu::notifier pcpu_porting_notifier(pcpu_porting_init);

struct pcpu *__pcpu_find(int cpu)
{
    return pcpu.for_cpu(sched::cpus[cpu]);
}

struct pcpu *pcpu_this(void)
{
    return &*pcpu;
}
