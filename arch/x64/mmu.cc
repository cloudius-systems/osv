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
#include <osv/elf.hh>
#include "exceptions.hh"
#include "tls-switch.hh"

void page_fault(exception_frame *ef)
{
    arch::tls_switch_on_exception_stack tls_switch;
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);
    sched::exception_guard g;
    auto addr = processor::read_cr2();
    if (fixup_fault(ef)) {
        return;
    }
    auto pc = reinterpret_cast<void*>(ef->rip);
    if (!pc) {
        abort("trying to execute null pointer");
    }
    if (reinterpret_cast<void*>(addr & ~(mmu::page_size - 1)) == elf::missing_symbols_page_addr) {
        abort("trying to execute or access missing symbol");
    }
    // The following code may sleep. So let's verify the fault did not happen
    // when preemption was disabled, or interrupts were disabled.
    assert(sched::preemptable());
    assert(ef->rflags & processor::rflags_if);

    // And since we may sleep, make sure interrupts are enabled.
    DROP_LOCK(irq_lock) { // irq_lock is acquired by HW
        mmu::vm_fault(addr, ef);
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

inter_processor_interrupt tlb_flush_ipi{IPI_TLB_FLUSH, [] {
        mmu::flush_tlb_local();
        if (tlb_flush_pendingconfirms.fetch_add(-1) == 1) {
            tlb_flush_waiter.wake_from_kernel_or_with_irq_disabled();
        }
}};

void flush_tlb_all()
{
    static std::vector<sched::cpu*> ipis(sched::max_cpus);

    if (sched::cpus.size() <= 1) {
        mmu::flush_tlb_local();
        return;
    }

    SCOPE_LOCK(migration_lock);
    mmu::flush_tlb_local();
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_waiter.reset(*sched::thread::current());
    int count;
    if (sched::thread::current()->is_app()) {
        ipis.clear();
        std::copy_if(sched::cpus.begin(), sched::cpus.end(), std::back_inserter(ipis),
                [](sched::cpu* c) {
            if (c == sched::cpu::current()) {
                return false;
            }

            c->lazy_flush_tlb.store(true, std::memory_order_relaxed);
            if (!c->app_thread.load(std::memory_order_seq_cst)) {
                return false;
            }
            if (!c->lazy_flush_tlb.exchange(false, std::memory_order_relaxed)) {
                return false;
            }
            return true;
        });
        count = ipis.size();
    } else {
        count = sched::cpus.size() - 1;
    }
    tlb_flush_pendingconfirms.store(count);
    if (count == (int)sched::cpus.size() - 1) {
        tlb_flush_ipi.send_allbutself();
    } else {
        for (auto&& c: ipis) {
            tlb_flush_ipi.send(c);
        }
    }
    sched::thread::wait_until([] {
            return tlb_flush_pendingconfirms.load() == 0;
    });
    tlb_flush_waiter.clear();
}

static pt_element<4> page_table_root __attribute__((init_priority((int)init_prio::pt_root)));

pt_element<4> *get_root_pt(uintptr_t virt __attribute__((unused))) {
    return &page_table_root;
}

void switch_to_runtime_page_tables()
{
    processor::write_cr3(page_table_root.next_pt_addr());
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

bool is_page_fault_prot_write(unsigned int error_code) {
    return (error_code & (page_fault_write | page_fault_prot)) == (page_fault_write | page_fault_prot);
}

bool fast_sigsegv_check(uintptr_t addr, exception_frame* ef)
{
    if (is_page_fault_rsvd(ef->get_error())) {
        return true;
    }

    struct check_cow : public virt_pte_visitor {
        bool _result = false;
        void pte(pt_element<0> pte) override {
            _result = !pte_is_cow(pte) && !pte.writable();
        }
        void pte(pt_element<1> pte) override {
            // large ptes are never cow yet
        }
    } visitor;

    // if page is present, but write protected without cow bit set
    // it means that this address belong to PROT_READ vma, so no need
    // to search vma to verify permission
    if (is_page_fault_prot_write(ef->get_error())) {
        virt_visit_pte_rcu(addr, visitor);
        return visitor._result;
    }

    return false;
}

// The x86_64 is considered to conform to the von Neumann architecture with unified
// data and instruction caches. Therefore we do not need to do anything as they are always in sync.
void synchronize_cpu_caches(void *v, size_t size) {}
}
