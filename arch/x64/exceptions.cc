/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "exceptions.hh"
#include "dump.hh"
#include "mmu.hh"
#include "processor.hh"
#include "interrupt.hh"
#include <boost/format.hpp>
#include "sched.hh"
#include "debug.hh"
#include <libc/signal.hh>
#include <apic.hh>
#include "prio.hh"

typedef boost::format fmt;

__thread exception_frame* current_interrupt_frame;
interrupt_descriptor_table idt __attribute__((init_priority(IDT_INIT_PRIO)));

extern "C" {
    void ex_de();
    void ex_db();
    void ex_nmi();
    void ex_bp();
    void ex_of();
    void ex_br();
    void ex_ud();
    void ex_nm();
    void ex_pf();
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
        std::function<void ()> pre_eoi,
        std::function<void ()> eoi,
        std::function<void ()> handler)
{
    for (unsigned i = 32; i < 256; ++i) {
        if (!_handlers[i].post_eoi) {
            _handlers[i].eoi = eoi;
            _handlers[i].pre_eoi = pre_eoi;
            _handlers[i].post_eoi = handler;
            return i;
        }
    }
    abort();
}

unsigned interrupt_descriptor_table::register_level_triggered_handler(
        std::function<void ()> pre_eoi,
        std::function<void ()> handler)
{
    return register_interrupt_handler(pre_eoi, [] { apic->eoi(); }, handler);
}

unsigned interrupt_descriptor_table::register_handler(std::function<void ()> handler)
{
    return register_level_triggered_handler([] {}, handler);
}


void interrupt_descriptor_table::unregister_handler(unsigned vector)
{
    _handlers[vector].eoi = {};
    _handlers[vector].pre_eoi = {};
    _handlers[vector].post_eoi = {};
}

void interrupt_descriptor_table::invoke_interrupt(unsigned vector)
{
    _handlers[vector].pre_eoi();
    _handlers[vector].eoi();
    _handlers[vector].post_eoi();
}

extern "C" { void interrupt(exception_frame* frame); }

void interrupt(exception_frame* frame)
{
    // Rather that force the exception frame down the call stack,
    // remember it in a global here.  This works because our interrupts
    // don't nest.
    current_interrupt_frame = frame;
    unsigned vector = frame->error_code;
    idt.invoke_interrupt(vector);
    // must call scheduler after EOI, or it may switch contexts and miss the EOI
    current_interrupt_frame = nullptr;
    // FIXME: layering violation
    sched::preempt();
}

struct fault_fixup {
    ulong pc;
    ulong divert;
    friend bool operator<(fault_fixup a, fault_fixup b) {
        return a.pc < b.pc;
    }
};

extern fault_fixup fault_fixup_start[], fault_fixup_end[];

static void sort_fault_fixup() __attribute__((constructor(SORT_INIT_PRIO)));

static void sort_fault_fixup()
{
    std::sort(fault_fixup_start, fault_fixup_end);
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

extern "C" void nmi(exception_frame* ef)
{
    while (true) {
        processor::halt_no_interrupts();
    }
}

extern "C"
void general_protection(exception_frame* ef)
{
    if (fixup_fault(ef)) {
        return;
    }

    dump_registers(ef);

    abort("general protection fault\n");
}

#define DUMMY_HANDLER(x) \
     extern "C" void x(); void x() { abort("DUMMY_HANDLER for " #x " aborting.\n"); }

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
DUMMY_HANDLER(simd_exception)
