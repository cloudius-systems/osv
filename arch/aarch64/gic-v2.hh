/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef GIC_V2_HH
#define GIC_V2_HH

#include "gic-common.hh"

namespace gic {

class gic_v2_dist : public gic_dist {
public:
    gic_v2_dist(mmu::phys b, size_t l) : gic_dist(b, l) {}

    void enable();
    void disable();

    void write_reg_grp(gicd_reg_irq1, unsigned int irq, u8 value);
    void write_reg_grp(gicd_reg_irq2, unsigned int irq, u8 value);
};

enum class gicc_reg : unsigned int {
    GICC_CTLR   = 0x0000, /* CPU Interface Control Reg */
    GICC_PMR    = 0x0004, /* Interrupt Priority Mask Reg */
    GICC_BPR    = 0x0008, /* Binary Point Reg */
    GICC_IAR    = 0x000c, /* Interrupt Acklowledge Reg */
    GICC_EOIR   = 0x0010, /* End of Interrupt Reg */
    GICC_RPR    = 0x0014, /* Running Priority Reg */
    GICC_HPPIR  = 0x0018, /* Highest Priority Pending Interrupt Reg */
    GICC_ABPR   = 0x001c, /* Aliased Binary Point Reg */
    GICC_AIAR   = 0x0020, /* Aliased Interrupt Acknowledge Reg */
    GICC_AEOIR  = 0x0024, /* Aliased End of Interrupt Reg */
    GICC_AHPPIR = 0x0028, /* Aliased Highest Prio Pending Interrupt Reg */
    GICC_APR    = 0x00d0, /* Active Priorities Registers */
    GICC_NSAPR  = 0x00e0, /* Non-secure APR */
    GICC_IIDR   = 0x00fc, /* CPU Interface Identification Reg */
    GICC_DIR    = 0x1000  /* Deactivate Interrupt Reg */
    /* Note: we will not use GICC_DIR (the two-step mechanism) */
};

/* GIC CPU Interface */
class gic_v2_cpu {
public:
    gic_v2_cpu(mmu::phys b, size_t l);

    u32 read_reg(gicc_reg r);
    void write_reg(gicc_reg r, u32 value);

    void enable();
protected:
    mmu::phys _base;
};

class gic_v2_driver : public gic_driver {
public:
    gic_v2_driver(mmu::phys d, size_t d_len,
                  mmu::phys c, size_t c_len,
                  mmu::phys v2m, size_t v2m_len ) :
        _gicd(d, d_len), _gicc(c, c_len), _v2m_base(v2m)
    {
        if (v2m && v2m_len) {
            mmu::linear_map((void *)_v2m_base, _v2m_base, v2m_len, "gic_v2m",
                            mmu::page_size, mmu::mattr::dev);
        }
    }

    virtual void init_on_primary_cpu()
    {
        init_dist();
        init_cpuif(0);
        init_v2m();
    }

    virtual void init_on_secondary_cpu(int smp_idx) { init_cpuif(smp_idx); }

    virtual void mask_irq(unsigned int id);
    virtual void unmask_irq(unsigned int id);

    virtual void set_irq_type(unsigned int id, irq_type type);

    virtual void send_sgi(sgi_filter filter, int smp_idx, unsigned int vector);

    virtual unsigned int ack_irq();
    virtual void end_irq(unsigned int iar);

    virtual void allocate_msi_dev_mapping(pci::function* dev) {}

    virtual void initialize_msi_vector(unsigned int vector) { set_irq_type(vector, gic::irq_type::IRQ_TYPE_EDGE); }
    virtual void map_msi_vector(unsigned int vector, pci::function* dev, u32 target_cpu);
    virtual void unmap_msi_vector(unsigned int vector, pci::function* dev) {}
    virtual void msi_format(u64 *address, u32 *data, int vector);

private:
    void init_dist();
    void init_cpuif(int smp_idx);
    void init_v2m();

    gic_v2_dist _gicd;
    gic_v2_cpu _gicc;
    mmu::phys _v2m_base;
};

}
#endif
