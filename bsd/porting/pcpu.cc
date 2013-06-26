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
