/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "exceptions.hh"
#include "dump.hh"
#include <osv/mmu.hh>
#include "processor.hh"
#include <osv/interrupt.hh>
#include <boost/format.hpp>
#include <osv/sched.hh>
#include <osv/debug.hh>
#include <libc/signal.hh>
#include <apic.hh>
#include <osv/prio.hh>
#include <osv/rcu.hh>
#include <osv/mutex.h>
#include <osv/intr_random.hh>

#include "fault-fixup.hh"

typedef boost::format fmt;

__thread exception_frame* current_interrupt_frame;
interrupt_descriptor_table idt __attribute__((init_priority((int)init_prio::idt)));

extern "C" {
    void ex_de();
    void ex_db();
    void ex_nmi();
    void ex_bp();
    void ex_of();
    void ex_br();
    void ex_ud();
    void ex_nm();
    void ex_df();
    void ex_ts();
    void ex_np();
    void ex_ss();
    void ex_gp();
    void ex_pf();
    void ex_mf();
    void ex_ac();
    void ex_mc();
    void ex_xm();
}

interrupt_descriptor_table::interrupt_descriptor_table()
{

    add_entry(0, 1, ex_de);
    add_entry(1, 1, ex_db);
    add_entry(2, 1, ex_nmi);
    add_entry(3, 1, ex_bp);
    add_entry(4, 1, ex_of);
    add_entry(5, 1, ex_br);
    add_entry(6, 1, ex_ud);
    add_entry(7, 1, ex_nm);
    add_entry(8, 1, ex_df);
    add_entry(10, 1, ex_ts);
    add_entry(11, 1, ex_np);
    add_entry(12, 1, ex_ss);
    add_entry(13, 1, ex_gp);
    add_entry(14, 1, ex_pf);
    add_entry(16, 1, ex_mf);
    add_entry(17, 1, ex_ac);
    add_entry(18, 1, ex_mc);
    add_entry(19, 1, ex_xm);

    extern char interrupt_entry[];
    for (unsigned i = 32; i < 256; ++i) {
        add_entry(i, 2, reinterpret_cast<void (*)()>(interrupt_entry + (i - 32) * 16));
    }
}

void interrupt_descriptor_table::add_entry(unsigned vec, unsigned ist, void (*handler)())
{
    ulong addr = reinterpret_cast<ulong>(handler);
    idt_entry e = { };
    e.offset0 = addr;
    e.selector = processor::read_cs();
    // We can't take interrupts on the main stack due to the x86-64 redzone
    e.ist = ist;
    e.type = type_intr_gate;
    e.s = s_special;
    e.dpl = 0;
    e.p = 1;
    e.offset1 = addr >> 16;
    e.offset2 = addr >> 32;
    _idt[vec] = e;
}

void
interrupt_descriptor_table::load_on_cpu()
{
    processor::desc_ptr d(sizeof(_idt) - 1,
                               reinterpret_cast<ulong>(&_idt));
    processor::lidt(d);
}

unsigned interrupt_descriptor_table::register_interrupt_handler(
        std::function<bool ()> pre_eoi,
        std::function<void ()> eoi,
        std::function<void ()> post_eoi)
{
    WITH_LOCK(_lock) {
        for (unsigned i = 32; i < 256; ++i) {
            auto o = _handlers[i].read_by_owner();
            if (o == nullptr) {
                auto n = new handler(o, pre_eoi, eoi, post_eoi);

                _handlers[i].assign(n);
                osv::rcu_dispose(o);

                return i;
            }
        }
    }
    abort();
}

void interrupt_descriptor_table::unregister_handler(unsigned vector)
{
    WITH_LOCK(_lock) {
        auto o = _handlers[vector].read_by_owner();
        _handlers[vector].assign(nullptr);
        osv::rcu_dispose(o);
    }
}

shared_vector interrupt_descriptor_table::register_level_triggered_handler(
        unsigned gsi,
        std::function<bool ()> pre_eoi,
        std::function<void ()> post_eoi)
{
    WITH_LOCK(_lock) {
        for (unsigned i = 32; i < 256; ++i) {
            auto o = _handlers[i].read_by_owner();
            if ((o && o->gsi == gsi) || o == nullptr) {
                auto n = new handler(o, pre_eoi, [] { processor::apic->eoi(); }, post_eoi);
                n->gsi = gsi;

                _handlers[i].assign(n);
                osv::rcu_dispose(o);

                return shared_vector(i, n->id);
            }
        }
    }
    abort();
}

void interrupt_descriptor_table::unregister_level_triggered_handler(shared_vector v)
{
    auto vector = v.vector;
    auto id = v.id;
    WITH_LOCK(_lock) {
        auto o = _handlers[vector].read_by_owner();
        assert(o);
        interrupt_descriptor_table::handler *n;

        if (o->size() > 1) {
            // Remove shared vector with 'id' from handler
            n = new handler(o, id);
        } else {
            // Last shared vector is unregistered.
            n = nullptr;
        }
        _handlers[vector].assign(n);
        osv::rcu_dispose(o);
    }
}

unsigned interrupt_descriptor_table::register_handler(std::function<void ()> post_eoi)
{
    return register_interrupt_handler([] { return true; }, [] { processor::apic->eoi(); }, post_eoi);
}

void interrupt_descriptor_table::register_interrupt(inter_processor_interrupt *interrupt)
{
    unsigned v = register_handler(interrupt->get_handler());
    interrupt->set_vector(v);
}

void interrupt_descriptor_table::unregister_interrupt(inter_processor_interrupt *interrupt)
{
    unregister_handler(interrupt->get_vector());
}

void interrupt_descriptor_table::register_interrupt(gsi_edge_interrupt *interrupt)
{
    unsigned v = register_handler(interrupt->get_handler());
    interrupt->set_vector(v);
    interrupt->enable();
}

void interrupt_descriptor_table::unregister_interrupt(gsi_edge_interrupt *interrupt)
{
    interrupt->disable();
    unregister_handler(interrupt->get_vector());
}

void interrupt_descriptor_table::register_interrupt(gsi_level_interrupt *interrupt)
{
    shared_vector v = register_level_triggered_handler(interrupt->get_id(),
                                                       interrupt->get_ack(),
                                                       interrupt->get_handler());
    interrupt->set_vector(v);
    interrupt->enable();
}

void interrupt_descriptor_table::unregister_interrupt(gsi_level_interrupt *interrupt)
{
    interrupt->disable();
    unregister_level_triggered_handler(interrupt->get_vector());
}

void interrupt_descriptor_table::invoke_interrupt(unsigned vector)
{
    WITH_LOCK(osv::rcu_read_lock) {
        unsigned i, nr_shared;
        bool handled = false;

        auto ptr = _handlers[vector].read();
        if (!ptr) {
            return;
        }

        nr_shared = ptr->size();
        for (i = 0 ; i < nr_shared; i++) {
            handled = ptr->pre_eois[i]();
            if (handled) {
                break;
            }
        }

        ptr->eoi();

        if (handled) {
            ptr->post_eois[i]();
        }
    }
}

extern "C" { void interrupt(exception_frame* frame); }

void interrupt(exception_frame* frame)
{
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);
    // Rather that force the exception frame down the call stack,
    // remember it in a global here.  This works because our interrupts
    // don't nest.
    current_interrupt_frame = frame;
    unsigned vector = frame->error_code;
    harvest_interrupt_randomness(vector, frame);
    idt.invoke_interrupt(vector);
    // must call scheduler after EOI, or it may switch contexts and miss the EOI
    current_interrupt_frame = nullptr;
    // FIXME: layering violation
    sched::preempt();
}

bool fixup_fault(exception_frame* ef)
{
    fault_fixup v{ef->rip, 0};
    auto ff = std::lower_bound(fault_fixup_start, fault_fixup_end, v);
    if (ff != fault_fixup_end && ff->pc == ef->rip) {
        ef->rip = ff->divert;
        return true;
    }
    return false;
}

// Implement the various x86 exception handlers mentioned in arch/x64/entry.S.
// Note page_fault() is implemented in core/mmu.cc.

extern "C" void divide_error(exception_frame *ef);
void divide_error(exception_frame *ef)
{
    sched::exception_guard g;
    siginfo_t si;
    si.si_signo = SIGFPE;
    si.si_code = FPE_INTDIV;
    osv::generate_signal(si, ef);
}

extern "C" void simd_exception(exception_frame *ef)
{
    sched::exception_guard g;
    siginfo_t si;
    si.si_signo = SIGFPE;
    // FIXME: set si_code to one of FPE_FLTDIV, FPE_FLTOVF, FPE_FLTUND,
    // FPE_FLTRES, FPE_FLTINV or FPE_FLTSUB according to the exception
    // information in MXCSR. It is not a bitmask. See sigaction(2).
    si.si_code = 0;
    osv::generate_signal(si, ef);
}

extern "C" void nmi(exception_frame* ef)
{
    while (true) {
        processor::cli_hlt();
    }
}

extern "C"
void general_protection(exception_frame* ef)
{
    sched::exception_guard g;
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);
    if (fixup_fault(ef)) {
        return;
    }

    dump_registers(ef);

    abort("general protection fault\n");
}

#define DUMMY_HANDLER(x) \
     extern "C" void x(exception_frame* ef); void x(exception_frame *ef) { dump_registers(ef); abort("DUMMY_HANDLER for " #x " aborting.\n"); }

DUMMY_HANDLER(debug_exception)
DUMMY_HANDLER(breakpoint)
DUMMY_HANDLER(overflow)
DUMMY_HANDLER(bound_range_exceeded)
DUMMY_HANDLER(invalid_opcode)
DUMMY_HANDLER(device_not_available)
DUMMY_HANDLER(double_fault)
DUMMY_HANDLER(invalid_tss)
DUMMY_HANDLER(segment_not_present)
DUMMY_HANDLER(stack_fault)
DUMMY_HANDLER(math_fault)
DUMMY_HANDLER(alignment_check)
DUMMY_HANDLER(machine_check)
