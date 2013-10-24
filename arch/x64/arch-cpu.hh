/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_CPU_HH_
#define ARCH_CPU_HH_

#include "processor.hh"
#include "exceptions.hh"
#include "cpuid.hh"
#include "osv/pagealloc.hh"
#include <xmmintrin.h>

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

struct arch_cpu;
struct arch_thread;

struct arch_cpu {
    arch_cpu();
    processor::aligned_task_state_segment atss;
    init_stack initstack;
    // The per-CPU exception stack is used for nested exceptions.
    char percpu_exception_stack[4096] __attribute__((aligned(16)));
    u32 apic_id;
    u32 acpi_id;
    u64 gdt[nr_gdt];
    void init_on_cpu();
    void set_ist_entry(unsigned ist, char* base, size_t size);
    void set_exception_stack(char* base, size_t size);
    void set_exception_stack(arch_thread* t);
    void set_interrupt_stack(arch_thread* t);
    void enter_exception();
    void exit_exception();
};

struct arch_thread {
    char interrupt_stack[4096] __attribute__((aligned(16)));
    char exception_stack[4096] __attribute__((aligned(16)));
};


template <class T>
struct save_fpu {
    T state;
    // FIXME: xsave and friends
    typedef processor::fpu_state fpu_state;
    void save() { processor::fxsave(state.addr()); }
    void restore() { processor::fxrstor(state.addr()); }
};

struct fpu_state_alloc_page {
    processor::fpu_state* s =
            static_cast<processor::fpu_state*>(memory::alloc_page());
    processor::fpu_state *addr(){ return s; }
    ~fpu_state_alloc_page(){ memory::free_page(s); }
};

struct fpu_state_inplace {
    processor::fpu_state s;
    processor::fpu_state *addr() { return &s; }
} __attribute__((aligned(16)));

typedef save_fpu<fpu_state_alloc_page> arch_fpu;
typedef save_fpu<fpu_state_inplace> inplace_arch_fpu;


inline arch_cpu::arch_cpu()
    : gdt{0, 0x00af9b000000ffff, 0x00cf93000000ffff, 0x00cf9b000000ffff,
          0x0000890000000067, 0}
{
    auto tss_addr = reinterpret_cast<u64>(&atss.tss);
    gdt[gdt_tss] |= (tss_addr & 0x00ffffff) << 16;
    gdt[gdt_tss] |= (tss_addr & 0xff000000) << 32;
    gdt[gdt_tssx] = tss_addr >> 32;
    // Use the per-CPU stack for early boot faults.
    set_exception_stack(percpu_exception_stack, sizeof(percpu_exception_stack));
}

inline void arch_cpu::set_ist_entry(unsigned ist, char* base, size_t size)
{
    atss.tss.ist[ist] = reinterpret_cast<u64>(base + size);
}

inline void arch_cpu::set_exception_stack(char* base, size_t size)
{
    set_ist_entry(1, base, size);
}

inline void arch_cpu::set_exception_stack(arch_thread* t)
{
    auto& s = t->exception_stack;
    set_ist_entry(1, s, sizeof(s));
}

inline void arch_cpu::set_interrupt_stack(arch_thread* t)
{
    auto& s = t->interrupt_stack;
    set_ist_entry(2, s, sizeof(s));
}

inline void arch_cpu::init_on_cpu()
{
    using namespace processor;

    lgdt(desc_ptr(nr_gdt*8-1, reinterpret_cast<u64>(&gdt)));
    ltr(gdt_tss*8);
    idt.load_on_cpu();
    ulong cr4 = cr4_de | cr4_pse | cr4_pae | cr4_pge | cr4_osfxsr
            | cr4_osxmmexcpt;
    if (features().fsgsbase) {
        cr4 |= cr4_fsgsbase;
    }
    if (features().xsave) {
        cr4 |= cr4_osxsave;
    }
    write_cr4(cr4);

    // We can't trust the FPU and the MXCSR to be always initialized to default values.
    // In at least one particular version of Xen it is not, leading to SIMD exceptions.
    asm volatile ("fninit" ::: "memory");
    _mm_setcsr(0x1f80);
}

struct exception_guard {
    exception_guard();
    ~exception_guard();
};

}


#endif /* ARCH_CPU_HH_ */
