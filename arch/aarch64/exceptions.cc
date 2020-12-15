/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/prio.hh>
#include <osv/sched.hh>
#include <osv/interrupt.hh>

#include "exceptions.hh"
#include "fault-fixup.hh"
#include "dump.hh"

__thread exception_frame* current_interrupt_frame;
class interrupt_table idt __attribute__((init_priority((int)init_prio::idt)));

interrupt_desc::interrupt_desc(interrupt_desc *old, interrupt *interrupt)
{
    if (old) {
        *this = *old;
    }
    this->handlers.push_back(interrupt->get_handler());
    this->acks.push_back(interrupt->get_ack());
}

interrupt_desc::interrupt_desc(interrupt_desc *old)
{
    assert(old);
    *this = *old;
}

interrupt_table::interrupt_table() {
    debug_early_entry("interrupt_table::interrupt_table()");

    gic::gic->init_cpu(0);
    gic::gic->init_dist(0);

    this->nr_irqs = gic::gic->nr_irqs;
    debug_early("interrupt table: gic driver created.\n");
}

void interrupt_table::enable_irq(int id)
{
    gic::gic->unmask_irq(id);
}

void interrupt_table::disable_irq(int id)
{
    gic::gic->mask_irq(id);
}

void interrupt_table::register_interrupt(interrupt *interrupt)
{
    WITH_LOCK(_lock) {
        unsigned id = interrupt->get_id();
        assert(id < this->nr_irqs);
        interrupt_desc *old = this->irq_desc[id].read_by_owner();
        interrupt_desc *desc = new interrupt_desc(old, interrupt);
        this->irq_desc[id].assign(desc);
        osv::rcu_dispose(old);
        debug_early_u64(" registered IRQ id=", (u64)id);
        enable_irq(id);
    }
}

void interrupt_table::unregister_interrupt(interrupt *interrupt)
{
    WITH_LOCK(_lock) {
        unsigned id = interrupt->get_id();
        assert(id < this->nr_irqs);
        interrupt_desc *old = this->irq_desc[id].read_by_owner();
        if (!old) {
            disable_irq(id);
            return;
        }
        auto it = old->handlers.begin();
        for (; it < old->handlers.end(); it++) {
            if (it->target_type() == interrupt->get_handler().target_type()) {
                /* comparing std::function target() is not possible in general.
                 * Since we use lambdas only, this check should suffice for now */
                break;
            }
        }
        if (it == old->handlers.end()) {
            debug_early_u64("failed to unregister IRQ id=", (u64)id);
            disable_irq(id);
            return;
        }

        interrupt_desc *desc;

        if (old->handlers.size() > 1) {
            int i = std::distance(old->handlers.begin(), it);
            old->handlers.erase(old->handlers.begin() + i);
            old->acks.erase(old->acks.begin() + i);
            desc = new interrupt_desc(old);
        } else {
            // last handler for this interrupt id unregistered.
            desc = nullptr;
        }

        this->irq_desc[id].assign(desc);
        osv::rcu_dispose(old);
        debug_early_u64("unregistered IRQ id=", (u64)id);
        if (!desc) {
            disable_irq(id);
            return;
        }
    }
}

bool interrupt_table::invoke_interrupt(unsigned int id)
{
    WITH_LOCK(osv::rcu_read_lock) {
        assert(id < this->nr_irqs);
        interrupt_desc *desc = this->irq_desc[id].read();

        if (!desc || desc->handlers.empty()) {
            return false;
        }

        int i, nr_shared = desc->handlers.size();
        for (i = 0 ; i < nr_shared; i++) {
            if (desc->acks[i]()) {
                break;
            }
        }

        if (i < nr_shared) {
            desc->handlers[i]();
            return true;
        }

        return false;
    }
}

extern "C" { void interrupt(exception_frame* frame); }

void interrupt(exception_frame* frame)
{
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);
    /* remember frame in a global, need to change if going to nested */
    current_interrupt_frame = frame;

    unsigned int iar = gic::gic->ack_irq();
    unsigned int irq = iar & 0x3ff;

    /* note that special values 1022 and 1023 are used for
       group 1 and spurious interrupts respectively. */
    if (irq >= gic::gic->nr_irqs) {
        debug_early_u64("special InterruptID detected irq=", irq);

    } else {
        if (!idt.invoke_interrupt(irq))
            debug_early_u64("unhandled InterruptID irq=", irq);
        gic::gic->end_irq(iar);
    }

    current_interrupt_frame = nullptr;
    sched::preempt();
}

extern "C" { void handle_unexpected_sync_exception(exception_frame* frame); }

#define ESR_EC_BEG  26 // Exception Class field begins in ESR at the bit 26th
#define ESR_EC_END  31 // and ends at 31st
#define ESR_EC_MASK 0b111111UL

void handle_unexpected_sync_exception(exception_frame* frame)
{
    u64 exception_class = (frame->esr >> ESR_EC_BEG) & ESR_EC_MASK;
    debug_ll("unexpected synchronous exception, EC: 0x%04x\n", exception_class);
    dump_registers(frame);
}

bool fixup_fault(exception_frame* ef)
{
    fault_fixup v{ef->elr, 0};
    auto ff = std::lower_bound(fault_fixup_start, fault_fixup_end, v);
    if (ff != fault_fixup_end && ff->pc == ef->elr) {
        ef->elr = ff->divert;
        return true;
    }
    return false;
}
