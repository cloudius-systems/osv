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
    mmu::flush_tlb_local();
    if (sched::cpus.size() <= 1)
        return;
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_waiter.reset(*sched::thread::current());
    tlb_flush_pendingconfirms.store((int)sched::cpus.size() - 1);
    tlb_flush_ipi.send_allbutself();
    sched::thread::wait_until([] {
            return tlb_flush_pendingconfirms.load() == 0;
    });
    tlb_flush_waiter.clear();
}

}
