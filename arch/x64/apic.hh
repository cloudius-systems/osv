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

class apic_driver {
public:
    virtual ~apic_driver();
    virtual void self_ipi(unsigned vector) = 0;
    virtual void ipi(unsigned cpu, unsigned vector) = 0;
    virtual void eoi() = 0;
    virtual void write(apicreg reg, u32 value) = 0;
    void set_lvt(apiclvt reg, unsigned vector);
};

extern apic_driver* apic;

}

#endif /* APIC_HH_ */
