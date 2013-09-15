/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "sched.hh"
#include "osv/percpu.hh"
#include <bsd/porting/pcpu.h>
#include "debug.hh"

PERCPU(struct pcpu, pcpu);

struct pcpu *__pcpu_find(int cpu)
{
    return pcpu.for_cpu(sched::cpus[cpu]);
}

struct pcpu *pcpu_this(void)
{
    return &*pcpu;
}
