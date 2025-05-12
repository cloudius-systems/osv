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
#include <osv/kernel_config_logger_debug.h>
#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>

#include "exceptions.hh"
#include "fault-fixup.hh"
#include "dump.hh"
#include "gic-v3.hh"

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
#if CONF_logger_debug
    debug_early_entry("interrupt_table::interrupt_table()");
#endif
    this->msi_vector_base = 0;
    this->max_msi_vector = 0;

    gic::gic->init_on_primary_cpu();

    this->nr_irqs = gic::gic->nr_of_irqs();
#if CONF_logger_debug
    debug_early("interrupt table: gic driver created.\n");
#endif
}

void interrupt_table::enable_irq(int id)
{
    gic::gic->unmask_irq(id);
}

void interrupt_table::disable_irq(int id)
{
    gic::gic->mask_irq(id);
}

void interrupt_table::enable_msi_vector(unsigned vector)
{
    gic::gic->initialize_msi_vector(vector);
    gic::gic->unmask_irq(vector);
}

unsigned interrupt_table::register_handler(std::function<void ()> handler)
{
    unsigned vector = next_msi_vector.fetch_add(1);
    unsigned index = vector - msi_vector_base;
    if (index >= max_msi_handlers) {
        abort("The MSI vector %d too large\n", index);
    }

    msi_handlers[index] = handler;
    enable_msi_vector(vector);
    return vector;
}

void interrupt_table::unregister_handler(unsigned vector)
{
    unsigned index = vector - msi_vector_base;
    if (index >= max_msi_handlers) {
        abort("The MSI vector %d too large\n", index);
    }
    msi_handlers[index] = nullptr;
    disable_irq(vector);
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
#if CONF_logger_debug
        debug_early_u64(" registered IRQ id=", (u64)id);
#endif
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
#if CONF_logger_debug
        debug_early_u64("unregistered IRQ id=", (u64)id);
#endif
        if (!desc) {
            disable_irq(id);
            return;
        }
    }
}

void interrupt_table::init_msi_vector_base(u32 initial)
{
    msi_vector_base = initial;
    next_msi_vector.store(initial);
}

bool interrupt_table::invoke_interrupt(unsigned int iar)
{
#if CONF_lazy_stack_invariant
    assert(!arch::irq_enabled());
#endif
    WITH_LOCK(osv::rcu_read_lock) {
        // First see if it is an MSI vector and handle it
        if (iar && iar >= msi_vector_base && iar <= max_msi_vector) {
            unsigned handler_idx = iar - msi_vector_base;
            if (handler_idx >= max_msi_handlers || !msi_handlers[handler_idx]) {
                // This should never happen unless there is some bug
                // in the MSI configuration code
                debug_early_u64("misconfigured MSI interruptID iar=", iar);
                assert(0);
            } else {
                msi_handlers[handler_idx]();
            }
            return true;
        }

        unsigned int irq = iar & 0x3ff;
        if (irq >= gic::gic->nr_of_irqs()) {
            // Note that special values 1022 and 1023 are used for
            // group 1 and spurious interrupts respectively.
            debug_early_u64("special InterruptID detected irq=", irq);
            return false;
        } else {
            interrupt_desc *desc = this->irq_desc[irq].read();

            if (!desc || desc->handlers.empty()) {
                debug_early_u64("unhandled InterruptID irq=", irq);
                return true;
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

            debug_early_u64("unhandled InterruptID irq=", irq);
            return true;
        }
    }
}

extern "C" { void interrupt(exception_frame* frame); }

void interrupt(exception_frame* frame)
{
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);

    // Rather that force the exception frame down the call stack,
    // remember it in a global here. This works because our interrupts
    // don't nest.
    current_interrupt_frame = frame;

    unsigned int iar = gic::gic->ack_irq();
    if (idt.invoke_interrupt(iar)) {
        gic::gic->end_irq(iar);
    }

    current_interrupt_frame = nullptr;
    sched::preempt();
}

extern "C" { void handle_unexpected_exception(exception_frame* frame, u64 level, u64 type); }

#define EX_TYPE_SYNC 0x0
#define EX_TYPE_IRQ 0x1
#define EX_TYPE_FIQ 0x2
#define EX_TYPE_SERROR 0x3

#define ESR_EC_BEG  26 // Exception Class field begins in ESR at the bit 26th
#define ESR_EC_END  31 // and ends at 31st
#define ESR_EC_MASK 0b111111UL

void handle_unexpected_exception(exception_frame* frame, u64 level, u64 type)
{
    switch (type) {
        case EX_TYPE_SYNC:
           {
               u64 exception_class = (frame->esr >> ESR_EC_BEG) & ESR_EC_MASK;
               debug_ll("unexpected synchronous exception at level:%ld, EC: 0x%04x\n", level, exception_class);
           }
           break;
        case EX_TYPE_IRQ:
           debug_ll("unexpected IRQ exception at level:%ld\n", level);
           break;
        case EX_TYPE_FIQ:
           debug_ll("unexpected FIQ exception at level:%ld\n", level);
           break;
        case EX_TYPE_SERROR:
           debug_ll("unexpected system error at level:%ld\n", level);
           break;
        default:
           debug_ll("unexpected exception type:%ld at level:%ld\n", type, level);
    }
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
