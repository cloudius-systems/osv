/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch-cpu.hh"
#include <osv/debug.hh>
#include <osv/sched.hh>
#include <osv/mmu.hh>
#include <osv/irqlock.hh>
#include <osv/interrupt.hh>
#include <osv/migration-lock.hh>
#include <osv/prio.hh>
#include "exceptions.hh"

void page_fault(exception_frame *ef)
{
    sched::exception_guard g;
    auto addr = processor::read_cr2();
    if (fixup_fault(ef)) {
        return;
    }
    auto pc = reinterpret_cast<void*>(ef->rip);
    if (!pc) {
        abort("trying to execute null pointer");
    }
    // The following code may sleep. So let's verify the fault did not happen
    // when preemption was disabled, or interrupts were disabled.
    assert(sched::preemptable());
    assert(ef->rflags & processor::rflags_if);

    // And since we may sleep, make sure interrupts are enabled.
    DROP_LOCK(irq_lock) { // irq_lock is acquired by HW
        sched::inplace_arch_fpu fpu;
        fpu.save();
        mmu::vm_fault(addr, ef);
        fpu.restore();
    }
}

namespace mmu {

uint8_t phys_bits = max_phys_bits, virt_bits = 52;

void flush_tlb_local() {
    // TODO: we can use page_table_root instead of read_cr3(), can be faster
    // when shadow page tables are used.
    processor::write_cr3(processor::read_cr3());
}

// tlb_flush() does TLB flush on *all* processors, not returning before all
// processors confirm flushing their TLB. This is slow, but necessary for
// correctness so that, for example, after mprotect() returns, no thread on
// no cpu can write to the protected page.
mutex tlb_flush_mutex;
sched::thread_handle tlb_flush_waiter;
std::atomic<int> tlb_flush_pendingconfirms;

inter_processor_interrupt tlb_flush_ipi{[] {
        mmu::flush_tlb_local();
        if (tlb_flush_pendingconfirms.fetch_add(-1) == 1) {
            tlb_flush_waiter.wake();
        }
}};

void flush_tlb_all()
{
    if (sched::cpus.size() <= 1) {
        mmu::flush_tlb_local();
        return;
    }

    SCOPE_LOCK(migration_lock);
    mmu::flush_tlb_local();
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_waiter.reset(*sched::thread::current());
    tlb_flush_pendingconfirms.store((int)sched::cpus.size() - 1);
    tlb_flush_ipi.send_allbutself();
    sched::thread::wait_until([] {
            return tlb_flush_pendingconfirms.load() == 0;
    });
    tlb_flush_waiter.clear();
}

static pt_element page_table_root __attribute__((init_priority((int)init_prio::pt_root)));

void switch_to_runtime_page_tables()
{
    processor::write_cr3(page_table_root.next_pt_addr());
}

pt_element *get_root_pt(uintptr_t virt __attribute__((unused))) {
    return &page_table_root;
}

pt_element make_empty_pte() { return pt_element(); }

pt_element make_pte(phys addr, bool large, unsigned perm)
{
    pt_element pte;
    pte.set_valid(perm != 0);
    pte.set_writable(perm & perm_write);
    pte.set_executable(perm & perm_exec);
    pte.set_dirty(true);
    pte.set_large(large);
    pte.set_addr(addr, large);

    arch_pt_element::set_user(&pte, true);
    arch_pt_element::set_accessed(&pte, true);

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

enum {
    page_fault_prot  = 1ul << 0,
    page_fault_write = 1ul << 1,
    page_fault_user  = 1ul << 2,
    page_fault_rsvd  = 1ul << 3,
    page_fault_insn  = 1ul << 4,
};

bool is_page_fault_insn(unsigned int error_code) {
    return error_code & page_fault_insn;
}

bool is_page_fault_write(unsigned int error_code) {
    return error_code & page_fault_write;
}

bool is_page_fault_rsvd(unsigned int error_code) {
    return error_code & page_fault_rsvd;
}

/* Glauber Costa: if page faults because we are trying to execute code here,
 * we shouldn't be closing the balloon. We should [...] despair.
 * So by checking only for == page_fault_write, we are guaranteed to close
 * the balloon in the next branch - which although still bizarre, at least
 * will give us tracing information that I can rely on for debugging that.
 * (the reason for that is that there are only fixups for memcpy, and memcpy
 * can only be used to read or write).
 * The other bits like present and user should not matter in this case.
 */
bool is_page_fault_write_exclusive(unsigned int error_code) {
    return error_code == page_fault_write;
}

bool fast_sigsegv_check(uintptr_t addr, exception_frame* ef)
{
    if (is_page_fault_rsvd(ef->get_error())) {
        return true;
    }
    return false;
}
}
