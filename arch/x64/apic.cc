#include "apic.hh"
#include "msr.hh"

namespace processor {

class x2apic : public apic_driver {
public:
    explicit x2apic();
    virtual void init_on_ap();
    virtual void write(apicreg reg, u32 value);
    virtual void self_ipi(unsigned vector);
    virtual void ipi(unsigned apic_id, unsigned vector);
    virtual void ipi_allbutself(unsigned vector);
    virtual void eoi();
    virtual u32 id();
private:
    void enable();
};

apic_driver::~apic_driver()
{
}

void apic_driver::software_enable()
{
    write(apicreg::LVT0, 0);
    write(apicreg::SPIV, 0x1ff); // FIXME: allocate real vector
}

x2apic::x2apic()
    : apic_driver()
{
    enable();
    software_enable();
}

void x2apic::init_on_ap()
{
    enable();
}

void x2apic::enable()
{
    processor::wrmsr(msr::IA32_APIC_BASE, _apic_base_lo | (3 << 10));
    software_enable();
}

void x2apic::self_ipi(unsigned vector)
{
    wrmsr(msr::X2APIC_SELF_IPI, vector);
}

void x2apic::ipi(unsigned apic_id, unsigned vector)
{
    wrmsr(msr::X2APIC_ICR, vector | (u64(apic_id) << 32) | (1 << 14));
}

static constexpr unsigned APIC_SHORTHAND_SELF = 0x40000;
static constexpr unsigned APIC_SHORTHAND_ALL =  0x80000;
static constexpr unsigned APIC_SHORTHAND_ALLBUTSELF = 0xC0000;

void x2apic::ipi_allbutself(unsigned vector)
{
    wrmsr(msr::X2APIC_ICR, vector | APIC_SHORTHAND_ALLBUTSELF | (1 << 14));
}


void x2apic::eoi()
{
    wrmsr(msr::X2APIC_EOI, 0);
}

void x2apic::write(apicreg reg, u32 value)
{
    processor::wrmsr(0x800 + unsigned(reg) / 0x10, value);
}

u32 x2apic::id()
{
    return processor::rdmsr(msr::X2APIC_ID);
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
