#ifndef PER_CPU_COUNTER_HH_
#define PER_CPU_COUNTER_HH_

#include <osv/types.h>
#include <sched.hh>
#include <osv/percpu.hh>
#include <sched.hh>
#include <vector>
#include <memory>

class per_cpu_counter {
public:
    explicit per_cpu_counter();
    ~per_cpu_counter();
    void increment();
    ulong read();
private:
    unsigned _index;
private:
    static percpu<ulong*> _counters;
    static sched::cpu::notifier _cpu_notifier;
    static void init_on_cpu();
};

#endif /* PER_CPU_COUNTER_HH_ */
