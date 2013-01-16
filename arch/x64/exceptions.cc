#include "exceptions.hh"
#include "mmu.hh"
#include "processor.hh"
#include "interrupt.hh"
#include <boost/format.hpp>
#include "debug.hh"

typedef boost::format fmt;

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

    add_entry(0, ex_de);
    add_entry(1, ex_db);
    add_entry(2, ex_nmi);
    add_entry(3, ex_bp);
    add_entry(4, ex_of);
    add_entry(5, ex_br);
    add_entry(6, ex_ud);
    add_entry(7, ex_nm);
    add_entry(8, ex_df);
    add_entry(10, ex_ts);
    add_entry(11, ex_np);
    add_entry(12, ex_ss);
    add_entry(13, ex_gp);
    add_entry(14, ex_pf);
    add_entry(16, ex_mf);
    add_entry(17, ex_ac);
    add_entry(18, ex_mc);
    add_entry(19, ex_xm);

    extern char interrupt_entry[];
    for (unsigned i = 32; i < 256; ++i) {
        add_entry(i, reinterpret_cast<void (*)()>(interrupt_entry + (i - 32) * 16));
    }
}

void interrupt_descriptor_table::add_entry(unsigned vec, void (*handler)())
{
    ulong addr = reinterpret_cast<ulong>(handler);
    idt_entry e = { };
    e.offset0 = addr;
    e.selector = processor::read_cs();
    e.ist = 0;
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
    _handlers[vector] = std::function<void ()>();
}

void interrupt_descriptor_table::invoke_interrupt(unsigned vector)
{
    _handlers[vector]();
}

extern "C" { void interrupt(exception_frame* frame); }

void interrupt(exception_frame* frame)
{
    unsigned vector = frame->error_code;
    debug(fmt("interrupt %x") % vector);
    idt.invoke_interrupt(vector);
    processor::wrmsr(0x80b, 0); // EOI
}

msi_interrupt_handler::msi_interrupt_handler(std::function<void ()> handler)
    : _vector(idt.register_handler(handler))
    , _handler(handler)
{
}

msi_interrupt_handler::~msi_interrupt_handler()
{
    idt.unregister_handler(_vector);
}

msi_message msi_interrupt_handler::config()
{
    msi_message ret;
    ret.addr = 0xfee00000;
    ret.data = 0x4000 | _vector;
    return ret;
}

#define DUMMY_HANDLER(x) \
     extern "C" void x(); void x() { abort(); }

DUMMY_HANDLER(divide_error)
DUMMY_HANDLER(debug_exception)
DUMMY_HANDLER(nmi)
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
