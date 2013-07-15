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
    void increment();
    ulong read();
private:
    dynamic_percpu<ulong> _counter;
};

#endif /* PER_CPU_COUNTER_HH_ */
