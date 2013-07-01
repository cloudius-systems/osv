#include "exceptions.hh"
#include "mmu.hh"
#include "processor.hh"
#include "interrupt.hh"
#include <boost/format.hpp>
#include "sched.hh"
#include "debug.hh"
#include <libc/signal.hh>

typedef boost::format fmt;

__thread exception_frame* current_interrupt_frame;
interrupt_descriptor_table idt __attribute__((init_priority(20000)));

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

unsigned interrupt_descriptor_table::register_handler(std::function<void ()> handler)
{
    for (unsigned i = 32; i < 256; ++i) {
        if (!_handlers[i]) {
            _handlers[i] = handler;
            return i;
        }
    }
    abort();
}

void interrupt_descriptor_table::unregister_handler(unsigned vector)
{
    _handlers[vector] = {};
}

void interrupt_descriptor_table::invoke_interrupt(unsigned vector)
{
    _handlers[vector]();
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
    processor::wrmsr(0x80b, 0); // EOI
    // must call scheduler after EOI, or it may switch contexts and miss the EOI
    current_interrupt_frame = nullptr;
    // FIXME: layering violation
    sched::preempt();
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
DUMMY_HANDLER(general_protection)
DUMMY_HANDLER(math_fault)
DUMMY_HANDLER(alignment_check)
DUMMY_HANDLER(machine_check)
DUMMY_HANDLER(simd_exception)
