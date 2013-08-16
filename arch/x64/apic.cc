#include "apic.hh"
#include "msr.hh"
#include "xen.hh"
#include <osv/percpu.hh>
#include <cpuid.hh>
#include <processor.hh>
#include <mmu.hh>

namespace processor {

static constexpr u32 msr_kvm_pv_eoi = 0x4b564d04;
static PERCPU(u32, kvm_pv_eoi_word);

void kvm_pv_eoi_init()
{
    if (features().kvm_pv_eoi) {
        wrmsr(msr_kvm_pv_eoi, mmu::virt_to_phys(&*kvm_pv_eoi_word) | 1);
    }
}

inline bool try_fast_eoi()
{
    u8 r;
    asm("btr %2, %0; setc %1" : "+m"(*kvm_pv_eoi_word), "=rm"(r) : "r"(0));
    return r;
}

class x2apic : public apic_driver {
public:
    explicit x2apic();
    virtual void init_on_ap();
    virtual u32 read(apicreg reg);
    virtual void write(apicreg reg, u32 value);
    virtual void self_ipi(unsigned vector);
    virtual void ipi(unsigned apic_id, unsigned vector);
    virtual void init_ipi(unsigned apic_id, unsigned vector);
    virtual void ipi_allbutself(unsigned vector);
    virtual void nmi_allbutself();
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

void x2apic::init_ipi(unsigned apic_id, unsigned vector)
{
    wrmsr_safe(msr::X2APIC_ICR, vector | (u64(apic_id) << 32));
}

static constexpr unsigned APIC_SHORTHAND_SELF = 0x40000;
static constexpr unsigned APIC_SHORTHAND_ALL =  0x80000;
static constexpr unsigned APIC_SHORTHAND_ALLBUTSELF = 0xC0000;

void x2apic::ipi_allbutself(unsigned vector)
{
    wrmsr(msr::X2APIC_ICR, vector | APIC_SHORTHAND_ALLBUTSELF | (1 << 14));
}

void x2apic::nmi_allbutself()
{
    wrmsr(msr::X2APIC_ICR, APIC_SHORTHAND_ALLBUTSELF | (4 << 8) | (1 << 14));
}

void x2apic::eoi()
{
    if (try_fast_eoi()) {
        return;
    }
    wrmsr(msr::X2APIC_EOI, 0);
}

u32 x2apic::read(apicreg reg)
{
    return processor::rdmsr(0x800 + unsigned(reg) / 0x10);
}

void x2apic::write(apicreg reg, u32 value)
{
    processor::wrmsr(0x800 + unsigned(reg) / 0x10, value);
}

u32 x2apic::id()
{
    u32 id = processor::rdmsr(msr::X2APIC_ID);
    if (!is_xen())
        return id;

    // The x2APIC specification says that reading from the X2APIC_ID MSR should
    // return the physical apic id of the current processor. However, the Xen
    // implementation (as of 4.2.2) is broken, and reads actually return old
    // style xAPIC id. Even if they fix it, we still have HVs deployed around
    // that will return the wrong ID. We can work around this by testing if the
    // returned APIC id is in the form (id << 24), since in that case, the
    // first 24 bits will all be zeroed. Then at least we can get this working
    // everywhere. This may pose a problem if we want to ever support more than
    // 1 << 24 vCPUs (or if any other HV has some random x2apic ids), but that
    // is highly unlikely anyway.
    if (((id & 0xffffff) == 0) && ((id >> 24) != 0)) {
        id = (id >> 24);
    }
    return id;
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
