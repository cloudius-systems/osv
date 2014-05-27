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

static pt_element page_table_root[2] __attribute__((init_priority((int)init_prio::pt_root)));

void switch_to_runtime_page_tables()
{
    auto pt_ttbr0 = mmu::page_table_root[0].next_pt_addr();
    auto pt_ttbr1 = mmu::page_table_root[1].next_pt_addr();
    asm volatile("msr ttbr0_el1, %0; isb;" ::"r" (pt_ttbr0));
    asm volatile("msr ttbr1_el1, %0; isb;" ::"r" (pt_ttbr1));
    mmu::flush_tlb_all();
}

pt_element *get_root_pt(uintptr_t virt)
{
    return &page_table_root[virt >> 63];
}

pt_element make_empty_pte() { return pt_element(); }

pt_element make_pte(phys addr, bool large,
                    unsigned perm = perm_read | perm_write | perm_exec)
{
    pt_element pte;
    pte.set_valid(perm != 0);
    pte.set_writable(perm & perm_write);
    pte.set_executable(perm & perm_exec);
    pte.set_dirty(true);
    pte.set_large(large);
    pte.set_addr(addr, large);

    arch_pt_element::set_user(&pte, false);
    arch_pt_element::set_accessed(&pte, true);
    arch_pt_element::set_share(&pte, true);

    if (addr >= mmu::device_range_start && addr < mmu::device_range_stop) {
        /* we need to mark device memory as such, because the
           semantics of the load/store instructions change */
        debug_early_u64("make_pte: device memory at ", (u64)addr);
        arch_pt_element::set_attridx(&pte, 0);
    } else {
        arch_pt_element::set_attridx(&pte, 4);
    }

    return pte;
}

pt_element make_normal_pte(phys addr, unsigned perm)
{
    return make_pte(addr, false, perm);
}

pt_element make_large_pte(phys addr, unsigned perm)
{
    return make_pte(addr, true, perm);
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
}
