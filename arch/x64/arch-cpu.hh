#ifndef ARCH_CPU_HH_
#define ARCH_CPU_HH_

#include "processor.hh"

namespace sched {

struct arch_cpu {
    processor::aligned_task_state_segment atss;
    u32 apic_id;
    u32 acpi_id;
    void init_on_cpu();
};

}


#endif /* ARCH_CPU_HH_ */
