/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef APIC_HH_
#define APIC_HH_

#include <osv/types.h>
#include <osv/mmu.hh>

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
    timer = static_cast<int>(apicreg::LVTT),
    lint0 = static_cast<int>(apicreg::LVT0),
    lint1 = static_cast<int>(apicreg::LVT1),
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
    apic_driver() { read_base(); }
    virtual ~apic_driver() {}
    virtual void init_on_ap() { enable(); }
    virtual void self_ipi(unsigned vector) = 0;
    virtual void ipi(unsigned apic_id, unsigned vector) = 0;
    virtual void init_ipi(unsigned apic_id, unsigned vector) = 0;
    virtual void ipi_allbutself(unsigned vector) = 0;
    virtual void nmi_allbutself() = 0;
    virtual void eoi() = 0;
    virtual u32 read(apicreg reg) = 0;
    virtual void write(apicreg reg, u32 value) = 0;
    virtual u32 id() = 0;

    // vector should be above 31, below 15 will fail
    // dest_id is the apic id, if using an io_apic.
    msi_message compose_msix(u8 vector, u8 dest_id);
protected:
    virtual void software_enable();
    virtual void enable() = 0;

    mmu::phys _apic_base;

    static constexpr unsigned APIC_SHORTHAND_SELF = 0x40000;
    static constexpr unsigned APIC_SHORTHAND_ALL =  0x80000;
    static constexpr unsigned APIC_SHORTHAND_ALLBUTSELF = 0xC0000;
    static constexpr unsigned APIC_ICR_TYPE_FIXED = 0x00000;
    static constexpr unsigned APIC_ICR_LEVEL_ASSERT = 1 << 14;
    static constexpr unsigned APIC_BASE_GLOBAL_ENABLE = 1 << 11;

    static u32 apic_delivery(u32 mode) { return mode << DELIVERY_SHIFT; }

private:
    void read_base();
    static constexpr unsigned DELIVERY_SHIFT = 8;
};

extern apic_driver* apic;

void kvm_pv_eoi_init();

}

#endif /* APIC_HH_ */
