#include "apic.hh"
#include "msr.hh"

namespace processor {

class x2apic : public apic_driver {
public:
    explicit x2apic();
    virtual void self_ipi(unsigned vector);
    virtual void ipi(unsigned cpu, unsigned vector);
    virtual void eoi();
};

apic_driver::~apic_driver()
{
}

x2apic::x2apic()
{
    processor::wrmsr(msr::IA32_APIC_BASE, 0xfee00000 | (3 << 10));
}

void x2apic::self_ipi(unsigned vector)
{
    wrmsr(msr::X2APIC_SELF_IPI, vector);
}

void x2apic::ipi(unsigned cpu, unsigned vector)
{
    // FIXME: don't assume APIC ID == cpu number
    wrmsr(msr::X2APIC_ICR, vector | (u64(cpu) << 32) | (1 << 14));
}

void x2apic::eoi()
{
    wrmsr(msr::X2APIC_EOI, 0);
}

apic_driver* create_apic_driver()
{
    // FIXME: detect
    return new x2apic;
}

apic_driver* apic = create_apic_driver();

}
