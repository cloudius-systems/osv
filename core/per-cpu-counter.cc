#include <osv/per-cpu-counter.hh>
#include <osv/mutex.h>
#include <debug.hh>

void per_cpu_counter::increment()
{
    sched::preempt_disable();
    ++*_counter;
    sched::preempt_enable();
}

ulong per_cpu_counter::read()
{
    ulong sum = 0;
    for (auto cpu : sched::cpus) {
        sum += *_counter.for_cpu(cpu);
    }
    return sum;
}
