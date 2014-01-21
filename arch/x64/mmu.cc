/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch.hh"
#include "arch-cpu.hh"
#include "debug.hh"
#include <osv/sched.hh>
#include <osv/mmu.hh>

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
    arch::irq_enable();

    sched::inplace_arch_fpu fpu;
    fpu.save();
    mmu::vm_fault(addr, ef);
    fpu.restore();
}
