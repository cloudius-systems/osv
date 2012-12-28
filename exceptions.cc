#include "exceptions.hh"
#include "mmu.hh"
#include "arch/x64/processor.hh"

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
