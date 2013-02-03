#ifndef ARCH_CPU_HH_
#define ARCH_CPU_HH_

#include "processor.hh"

struct init_stack {
    char stack[4096] __attribute__((aligned(16)));
    init_stack* next;
} __attribute__((packed));

namespace sched {

struct arch_cpu {
    processor::aligned_task_state_segment atss;
    init_stack initstack;
    u32 apic_id;
    u32 acpi_id;
    void init_on_cpu();
};

}


#endif /* ARCH_CPU_HH_ */
