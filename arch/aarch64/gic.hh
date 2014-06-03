/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef GIC_HH
#define GIC_HH

#include <osv/types.h>
#include <osv/mmu.hh>
#include <osv/mutex.h>

/* This is GICv2. Revisit for v3 if/when needed. */
namespace gic {

constexpr int max_cpu_if = 8;
constexpr int max_nr_irqs = 1020;

enum class gicd_reg : unsigned int {
    GICD_CTLR       = 0x000, /* Distributor Control Reg */
    GICD_TYPER      = 0x004, /* Interrupt Controller Type Reg */
    GICD_IDDR       = 0x008, /* Distributor Implementer Identification Reg */
    GICD_SGIR       = 0xf00, /* Software Generated Interrupt Register */
    ICPIDR2         = 0xfe8, /* Peripheral ID2 Register */
};

/* the following are groups of 32 x 32bit bitfield registers,
   with 1 bit/flag assigned to each interrupt (32x32=1024) */
enum class gicd_reg_irq1 : unsigned int {
    GICD_IGROUPR    = 0x080, /* Interrupt Group Registers */
    GICD_ISENABLER  = 0x100, /* Interrupt Set-Enable Regs */
    GICD_ICENABLER  = 0x180, /* Interrupt Clear-Enable Regs */
    GICD_ISPENDR    = 0x200, /* Interrupt Set-Pending Regs */
    GICD_ICPENDR    = 0x280, /* Interrupt Clear-Pending Regs */
    GICD_ISACTIVER  = 0x300, /* Interrupt Set-Active Regs */
    GICD_ICACTIVER  = 0x380, /* Interrupt Clear-Active Regs */
};

/* the following are groups of  64 x 32bit bitfield registers,
   with 2 bits assigned to each interrupt (64x32/2=1024) */
enum class gicd_reg_irq2 : unsigned int {
    GICD_ICFGR      = 0xc00, /* Interrupt Configuration Regs */
    GICD_NSACR      = 0xe00, /* Non-secure Access Control Regs */
};

/* the following are groups of 256 x 32bit bitfield registers, byte access,
   with 1 byte assigned to each interrupt (256x32/8=1024) */
enum class gicd_reg_irq8 : unsigned int {
    GICD_IPRIORITYR = 0x400, /* Interrupt Priority Regs */
    GICD_ITARGETSR  = 0x800, /* Interrupt Processor Target Regs */
};

/* the following are groups of 4 x 32bit bitfield registers, byte access,
   with 1 byte assigned to each of the 16 SGIs (4x32/8=16) */
enum class gicd_reg_sgi8 : unsigned int {
    GICD_CPENDSGIR  = 0xf10,
    GICD_SPENDSGIR  = 0xf20,
};

/* GIC Distributor Interface */
class gic_dist {
public:
    gic_dist(mmu::phys b) : base(b) {}
    unsigned int read_reg(gicd_reg r);
    void write_reg(gicd_reg r, unsigned int);
    unsigned int read_reg_raw(unsigned int r, unsigned int);
    void write_reg_raw(unsigned int r, unsigned int, unsigned int);

    unsigned char read_reg_grp(gicd_reg_irq1, unsigned int);
    void write_reg_grp(gicd_reg_irq1, unsigned int, unsigned char);

    unsigned char read_reg_grp(gicd_reg_irq2, unsigned int);
    void write_reg_grp(gicd_reg_irq2, unsigned int, unsigned char);

    unsigned char read_reg_grp(gicd_reg_irq8, unsigned int);
    void write_reg_grp(gicd_reg_irq8, unsigned int, unsigned char);

    unsigned char read_reg_grp(gicd_reg_sgi8, unsigned int);
    void write_reg_grp(gicd_reg_sgi8, unsigned int, unsigned char);

protected:
    mmu::phys base;
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
class gic_cpu {
public:
    gic_cpu(mmu::phys b) : base(b) {}
    unsigned int read_reg(gicc_reg r);
    void write_reg(gicc_reg r, unsigned int);
protected:
    mmu::phys base;
};

enum class sgi_target_list_filter : unsigned int {
    SGI_TARGET_LIST = 0,
    SGI_TARGET_ALL_BUT_SELF = 1,
    SGI_TARGET_SELF = 2,

    SGI_TARGET_N
};

enum class irq_type : unsigned int {
    IRQ_TYPE_LEVEL = 0,
    IRQ_TYPE_EDGE = 1,

    IRQ_TYPE_N
};

class gic_driver {
public:
    gic_driver(mmu::phys d, mmu::phys c) : gicd(d), gicc(c) {}

    void init_cpu(int smp_idx); /* called on each cpu bringup */
    void init_dist(int smp_idx); /* only called by boot cpu once */

    void mask_irq(unsigned int id); /* disable a single InterruptID */
    void unmask_irq(unsigned int id); /* enable a single InterruptID */

    void set_irq_type(unsigned int id, irq_type type); /* set level or edge */

    /* send software-generated interrupt to other cpus; vector is [0..15] */
    void send_sgi(sgi_target_list_filter filter, unsigned char cpulist,
                  unsigned int vector); /* NIY */

    /* IAR: acknowledge irq, state pending=>active (or active/pending).
     * 31      ..     13 | 12     10 | 9            0 |
     *      Reserved     |   CPUID   |  Interrupt ID  |
     *
     * ack_irq returns the whole IAR register, further processing of result
     * depends on interrupt type.
     */
    unsigned int ack_irq(void);

    /* EOIR: inform interface about completion of interrupt processing */
    void end_irq(unsigned int);

public:
    class gic_dist gicd;
    class gic_cpu gicc;
    int nr_irqs;
protected:
    /* cpu_targets: our smp cpu index is not necessarily equal
       to the gic interface target. We query the gic to get
       the cpumask corresponding to each smp cpu (gic_init_on_cpu),
       and put the result here at the right smp index. */
    unsigned char cpu_targets[max_cpu_if];
    mutex gic_lock;
};

/* the gic driver class is created by the interrupt table class */
extern class gic_driver *gic;
}

#endif /* GIC_HH */
