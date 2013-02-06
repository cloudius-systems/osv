#ifndef ARCH_CPU_HH_
#define ARCH_CPU_HH_

#include "processor.hh"

struct init_stack {
    char stack[4096] __attribute__((aligned(16)));
    init_stack* next;
} __attribute__((packed));


enum  {
    gdt_null = 0,
    gdt_cs = 1,
    gdt_ds = 2,
    gdt_cs32 = 3,
    gdt_tss = 4,
    gdt_tssx = 5,

    nr_gdt
};

namespace sched {

struct arch_cpu {
    arch_cpu();
    processor::aligned_task_state_segment atss;
    init_stack initstack;
    char exception_stack[4096] __attribute__((aligned(16)));
    u32 apic_id;
    u32 acpi_id;
    u64 gdt[nr_gdt];
    void init_on_cpu();
};

inline arch_cpu::arch_cpu()
    : gdt{0, 0x00af9b000000ffff, 0x00cf93000000ffff, 0x00cf9b000000ffff,
          0x0000890000000067, 0}
{
    auto tss_addr = reinterpret_cast<u64>(&atss.tss);
    gdt[gdt_tss] |= (tss_addr & 0x00ffffff) << 16;
    gdt[gdt_tss] |= (tss_addr & 0xff000000) << 32;
    gdt[gdt_tssx] = tss_addr >> 32;
    atss.tss.ist[1] = reinterpret_cast<u64>(exception_stack + sizeof(exception_stack));
}

inline void arch_cpu::init_on_cpu()
{
    using namespace processor;

    lgdt(desc_ptr(nr_gdt*8-1, reinterpret_cast<u64>(&gdt)));
    ltr(gdt_tss*8);
}

}


#endif /* ARCH_CPU_HH_ */
