/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/prio.hh>
#include <osv/sched.hh>

#include "exceptions.hh"
#include "gic.hh"
#include "fault-fixup.hh"

__thread exception_frame* current_interrupt_frame;
class interrupt_table idt __attribute__((init_priority((int)init_prio::idt)));

interrupt_desc::interrupt_desc(struct interrupt_desc *old, void *o, int i,
                               interrupt_handler h, gic::irq_type t)
{
    if (old) {
        *this = *old;
    } else {
        this->id = i;
        this->type = t;
    }

    this->objs.push_back(o);
    this->handlers.push_back(h);
}

interrupt_table::interrupt_table() {
    debug_early_entry("interrupt_table::interrupt_table()");

    gic::gic->init_cpu(0);
    gic::gic->init_dist(0);

    this->nr_irqs = gic::gic->nr_irqs;
    debug_early("interrupt table: gic driver created.\n");
}

void interrupt_table::enable_irq(int i)
{
    WITH_LOCK(osv::rcu_read_lock) {
        struct interrupt_desc *desc = this->irq_desc[i].read();
        if (desc && !desc->handlers.empty()) {
            debug_early_u64("enable_irq: enabling InterruptID=", (u64)i);
            gic::gic->set_irq_type(desc->id, desc->type);
            gic::gic->unmask_irq(desc->id);
        } else {
            debug_early_u64("enable_irq: failed to enable InterruptID=", (u64)i);
        }
    }
}

void interrupt_table::register_handler(void *obj, int i,
                                       interrupt_handler h, gic::irq_type t)
{
    WITH_LOCK(_lock) {
        assert(i < this->nr_irqs);
        struct interrupt_desc *old = this->irq_desc[i].read_by_owner();
        struct interrupt_desc *desc = new interrupt_desc(old, obj, i, h, t);
        this->irq_desc[i].assign(desc);
        osv::rcu_dispose(old);
        debug_early_u64("registered IRQ id=", (u64)i);
    }
}

int interrupt_table::invoke_interrupt(int id)
{
    WITH_LOCK(osv::rcu_read_lock) {
        assert(id < this->nr_irqs);
        struct interrupt_desc *desc = this->irq_desc[id].read();

        if (!desc || desc->handlers.empty()) {
            return 0;
        }

        int nr_shared = desc->handlers.size();
        for (int i = 0 ; i < nr_shared; i++) {
            if (desc->handlers[i](desc->objs[i])) {
                return 1;
            }
        }

        return 0;
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
    int irq = iar & 0x3ff;

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
