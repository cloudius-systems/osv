/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef GIC_COMMON_HH
#define GIC_COMMON_HH

#include <osv/types.h>
#include <osv/mmu-defs.hh>
#include <osv/spinlock.h>

#define GIC_MAX_IRQ  1019
#define GIC_SPI_BASE 32
#define GIC_PPI_BASE 16

namespace pci {
class function;
}

namespace gic {

constexpr int max_nr_irqs = 1020;

enum class gicd_reg : unsigned int {
    GICD_CTLR       = 0x0000, /* Distributor Control Reg */
    GICD_TYPER      = 0x0004, /* Interrupt Controller Type Reg */
    GICD_IDDR       = 0x0008, /* Distributor Implementer Identification Reg */
    GICD_SGIR       = 0x0f00, /* Software Generated Interrupt Register */
    ICPIDR2         = 0x0fe8, /* Peripheral ID2 Register */
};

/* the following are groups of 32 x 32bit bitfield registers,
   with 1 bit/flag assigned to each interrupt (32x32=1024) */
enum class gicd_reg_irq1 : unsigned int {
    GICD_IGROUPR    = 0x0080, /* Interrupt Group Registers */
    GICD_ISENABLER  = 0x0100, /* Interrupt Set-Enable Regs */
    GICD_ICENABLER  = 0x0180, /* Interrupt Clear-Enable Regs */
    GICD_ISPENDR    = 0x0200, /* Interrupt Set-Pending Regs */
    GICD_ICPENDR    = 0x0280, /* Interrupt Clear-Pending Regs */
    GICD_ISACTIVER  = 0x0300, /* Interrupt Set-Active Regs */
    GICD_ICACTIVER  = 0x0380, /* Interrupt Clear-Active Regs */
};

/* the following are groups of  64 x 32bit bitfield registers,
   with 2 bits assigned to each interrupt (64x32/2=1024) */
enum class gicd_reg_irq2 : unsigned int {
    GICD_ICFGR      = 0x0c00, /* Interrupt Configuration Regs */
    GICD_NSACR      = 0x0e00, /* Non-secure Access Control Regs */
};

/* the following are groups of 256 x 32bit bitfield registers, byte access,
   with 1 byte assigned to each interrupt (256x32/8=1024) */
enum class gicd_reg_irq8 : unsigned int {
    GICD_IPRIORITYR = 0x0400, /* Interrupt Priority Regs */
    GICD_ITARGETSR  = 0x0800, /* Interrupt Processor Target Regs */
};

enum class sgi_filter : unsigned int {
    SGI_TARGET_LIST = 0,
    SGI_TARGET_ALL_BUT_SELF = 1,
    SGI_TARGET_SELF = 2,
};

enum class irq_type : unsigned int {
    IRQ_TYPE_LEVEL = 0,
    IRQ_TYPE_EDGE = 1,
};

// Interrupt Group Registers, GICD_IGROUPRn
// These registers provide a status bit for each interrupt supported by
// the GIC. Each bit controls whether the corresponding interrupt is in
// Group 0 or Group 1
#define GICD_I_PER_IGROUPRn     32
#define GICD_DEF_IGROUPRn       0xffffffff

#define GICD_ICFGR_DEF_TYPE     0
#define GICD_I_PER_ICFGRn       16

#define GICD_IPRIORITY_DEF      0xc0c0c0c0

#define GICD_DEF_ICACTIVERn     0xffffffff
#define GICD_DEF_ICENABLERn     0xffffffff

#define GICD_I_PER_ICACTIVERn   32
#define GICD_I_PER_IPRIORITYn   4

#define GICD_I_PER_ICENABLERn   32
#define GICD_I_PER_ISENABLERn   32

/* GIC Distributor Interface */
class gic_dist {
protected:
    gic_dist(mmu::phys b, size_t l);

public:
    u32 read_reg(gicd_reg r);
    void write_reg(gicd_reg r, u32 v);

    u32 read_reg_at_offset(u32 reg, u32 offset);
    void write_reg_at_offset(u32 reg, u32 offset, u32 value);

    void write_reg64_at_offset(u32 reg, u32 offset, u64 v);

    unsigned int read_number_of_interrupts();

protected:
    mmu::phys _base;
};

/* Base class with mostly virtual functions intended to provide
   abstraction of the GIC driver. The implementation of specific
   version (GICv2 or GICv3) should be provided by subclasses like
   gic_v2_driver. */
class gic_driver {
public:
    virtual ~gic_driver() {}

    virtual void init_on_primary_cpu() = 0;
    virtual void init_on_secondary_cpu(int smp_idx) = 0;

    virtual void mask_irq(unsigned int id) = 0;
    virtual void unmask_irq(unsigned int id) = 0;

    virtual void set_irq_type(unsigned int id, irq_type type) = 0;

    virtual void send_sgi(sgi_filter filter, int smp_idx, unsigned int vector) = 0;

    virtual unsigned int ack_irq() = 0;
    virtual void end_irq(unsigned int iar) = 0;

    unsigned int nr_of_irqs() { return _nr_irqs; }

    virtual void allocate_msi_dev_mapping(pci::function* dev) = 0;

    virtual void initialize_msi_vector(unsigned int vector) = 0;
    virtual void map_msi_vector(unsigned int vector, pci::function* dev, u32 target_cpu) = 0;
    virtual void unmap_msi_vector(unsigned int vector, pci::function* dev) = 0;
    virtual void msi_format(u64 *address, u32 *data, int vector) = 0;

protected:
    unsigned int _nr_irqs;
    spinlock_t gic_lock;
};

extern class gic_driver *gic;

}

#endif
