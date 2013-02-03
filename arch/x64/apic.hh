#ifndef APIC_HH_
#define APIC_HH_

#include "types.hh"

namespace processor {

enum class apicreg {
    ID = 0x20,
    LVR = 0x30,
    TPR = 0x80,
    APR = 0x90,
    PPR = 0xa0,
    EOR = 0xb0,
    RRR = 0xc0,
    LDR = 0xd0,
    DFR = 0xe0,
    SPIV = 0xf0,
    ISR = 0x100,
    TMR = 0x180,
    IRR = 0x200,
    ESR = 0x280,
    ICR = 0x300,
    ICR2 = 0x310,
    LVTT = 0x320,
    LVTTHMR = 0x330,
    LVTPC = 0x340,
    LVT0 = 0x350,
    LVT1 = 0x360,
    LVTERR = 0x370,
    TMICT = 0x380,
    TMCCT = 0x390,
    TMDCR = 0x3e0,
    SELF_IPI = 0x3f0,
};

enum class apiclvt {
    timer = apicreg::LVTT,
    lint0 = apicreg::LVT0,
    lint1 = apicreg::LVT1,
};

enum msi_data_fields {
    MSI_VECTOR = 0,
    MSI_DELIVERY_MODE = 8,
    MSI_LEVEL_ASSERTION = 14,
    MSI_TRIGGER_MODE = 15
};

enum delivery_mode {
    FIXED_DELIVERY = 0,
    LOWPRI_DELIVERY = 1,
    SMI_DELIVERY = 2,
    NMI_DELIVERY = 4,
    INIT_DELIVERY = 5,
    EXTINT_DELIVERY = 7
};

// Care only if trigger_mode=level
enum level_assertion {
    MSI_DEASSERT = 0,
    MSI_ASSSERT = 1
};

enum trigger_mode {
    TRIGGER_MODE_EDGE = 0,
    TRIGGER_MODE_LEVEL = 1
};

struct msi_message {
    msi_message() : _addr(0), _data(0) {}
    u64 _addr;
    u32 _data;
};

class apic_driver {
public:
    apic_driver() : _apic_base_lo(0xfee00000), _apic_base_hi(0) {}
    virtual ~apic_driver();
    virtual void init_on_ap() = 0;
    virtual void self_ipi(unsigned vector) = 0;
    virtual void ipi(unsigned cpu, unsigned vector) = 0;
    virtual void eoi() = 0;
    virtual void write(apicreg reg, u32 value) = 0;
    virtual u32 id() = 0;
    void set_lvt(apiclvt reg, unsigned vector);
    // vector should be above 31, below 15 will fail
    // dest_id is the apic id, if using an io_apic.
    msi_message compose_msix(u8 vector, u8 dest_id);
protected:
    u32 _apic_base_lo;
    u32 _apic_base_hi;
};

extern apic_driver* apic;

}

#endif /* APIC_HH_ */
