#include "apic.hh"
#include "msr.hh"

namespace processor {

class x2apic : public apic_driver {
public:
    explicit x2apic();
    virtual void write(apicreg reg, u32 value);
    virtual void self_ipi(unsigned vector);
    virtual void ipi(unsigned cpu, unsigned vector);
    virtual void eoi();
};

apic_driver::~apic_driver()
{
}

x2apic::x2apic()
    : apic_driver()
{
    processor::wrmsr(msr::IA32_APIC_BASE, _apic_base_lo | (3 << 10));
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

void x2apic::write(apicreg reg, u32 value)
{
    processor::wrmsr(0x800 + unsigned(reg) / 0x10, value);
}

apic_driver* create_apic_driver()
{
    // FIXME: detect
    return new x2apic;
}

apic_driver* apic = create_apic_driver();

void apic_driver::set_lvt(apiclvt source, unsigned vector)
{
    write(static_cast<apicreg>(source), vector);
}

msi_message apic_driver::compose_msix(u8 vector, u8 dest_id)
{
    msi_message msg;

    if (vector <= 15) {
        return msg;
    }

    msg._addr =
        ( (u64)_apic_base_hi << 32 ) |
        ( _apic_base_lo & 0xFFF00000 ) |
        ( dest_id << 12 );

    msg._data =
        ( delivery_mode::FIXED_DELIVERY << msi_data_fields::MSI_DELIVERY_MODE ) |
        ( level_assertion::MSI_ASSSERT << msi_data_fields::MSI_LEVEL_ASSERTION ) |
        ( trigger_mode::TRIGGER_MODE_EDGE << msi_data_fields::MSI_TRIGGER_MODE ) |
        vector;

    return msg;
}

}
