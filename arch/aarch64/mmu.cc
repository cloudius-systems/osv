/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mmu.hh>
#include <osv/prio.hh>
#include <osv/sched.hh>
#include <osv/debug.h>
#include <osv/irqlock.hh>

#include "arch-cpu.hh"
#include "exceptions.hh"

void page_fault(exception_frame *ef)
{
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);
    debug_early_entry("page_fault");
    u64 addr;
    asm volatile ("mrs %0, far_el1" : "=r"(addr));
    debug_early_u64("faulting address ", (u64)addr);
    debug_early_u64("elr exception ra ", (u64)ef->elr);

    if (fixup_fault(ef)) {
        debug_early("fixed up with fixup_fault\n");
        return;
    }

    if (!ef->elr) {
        debug_early("trying to execute null pointer\n");
        abort();
    }

    /* vm_fault might sleep, so check that the thread is preemptable,
     * and that interrupts in the saved pstate are enabled.
     * Then enable interrupts for the vm_fault.
     */
    assert(sched::preemptable());
    assert(!(ef->spsr & processor::daif_i));

    DROP_LOCK(irq_lock) {
        mmu::vm_fault(addr, ef);
    }

    debug_early("leaving page_fault()\n");
}

namespace mmu {

void flush_tlb_all() {
    asm volatile("dsb sy; tlbi vmalle1is; dsb sy; isb;");
}

void flush_tlb_local() {
    asm volatile("dsb sy; tlbi vmalle1; dsb sy; isb;");
}

static pt_element<4> page_table_root[2] __attribute__((init_priority((int)init_prio::pt_root)));
u64 mem_addr;

extern "C" { /* see boot.S */
    extern u64 smpboot_ttbr0;
    extern u64 smpboot_ttbr1;
}

void switch_to_runtime_page_tables()
{
    auto pt_ttbr0 = smpboot_ttbr0 = mmu::page_table_root[0].next_pt_addr();
    auto pt_ttbr1 = smpboot_ttbr1 = mmu::page_table_root[1].next_pt_addr();
    asm volatile("msr ttbr0_el1, %0; isb;" ::"r" (pt_ttbr0));
    asm volatile("msr ttbr1_el1, %0; isb;" ::"r" (pt_ttbr1));
    mmu::flush_tlb_all();
}

pt_element<4> *get_root_pt(uintptr_t virt)
{
    return &page_table_root[virt >> 63];
}

bool is_page_fault_insn(unsigned int esr) {
    unsigned int ec = esr >> 26;
    return ec == 0x20 || ec == 0x21;
}

bool is_page_fault_write(unsigned int esr) {
    unsigned int ec = esr >> 26;
    return (ec == 0x24 || ec == 0x25) && (esr & 0x40);
}

bool is_page_fault_write_exclusive(unsigned int esr) {
    return is_page_fault_write(esr);
}

bool fast_sigsegv_check(uintptr_t addr, exception_frame* ef) {
    return false;
}

void synchronize_cpu_caches(void *v, size_t size) {
    // The aarch64 qualifies as Modified Harvard architecture and defines separate
    // cpu instruction and data caches - I-cache and D-cache. Therefore it is necessary
    // to synchronize both caches by cleaning data cache and invalidating instruction
    // cache after loading code into memory before letting it be executed.
    // For more details of why and when it is necessary please read this excellent article -
    // https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/caches-and-self-modifying-code
    // or this paper - https://hal.inria.fr/hal-02509910/document.
    //
    // So when OSv dynamic linker, being part of the kernel code, loads pages
    // of executable sections of ELF segments into memory, we need to clean D-cache
    // in order push code (as data) into next cache level (L2) and invalidate
    // the I-cache right before it gets executed.
    //
    // In order to achieve the above we delegate to the __clear_cache builtin.
    // The __clear_cache does following in terms of ARM64 assembly:
    //
    // For each D-cache line in the range (v, v + size):
    //   DC CVAU, Xn ; Clean data cache by virtual address (VA) to PoU
    // DSB ISH       ; Ensure visibility of the data cleaned from cache
    // For each I-cache line in the range (v, v + size):
    //   IC IVAU, Xn ; Invalidate instruction cache by VA to PoU
    // DSB ISH       ; Ensure completion of the invalidations
    // ISB           ; Synchronize the fetched instruction stream
    //
    // Please note that that both DC CVAU and IC CVAU are broadcast to all cores in the
    // same Inner Sharebility domain (which all OSv memory is mapped as) so that all
    // caches in all cores should eventually see and execute same code.
    //
    // For more details about what this built-in does, please read this gcc documentation -
    // https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
    __builtin___clear_cache((char*)v, (char*)(v + size));
}
}
