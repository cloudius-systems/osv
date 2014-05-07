/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mutex.h>
#include <osv/debug.hh>
#include <osv/mmio.hh>

#include "gic.hh"

namespace gic {

class gic_driver *gic;

void gic_driver::init_cpu(int smp_idx)
{
    /* GICD_ITARGETSR[0..7] are "special" read-only registers
       which allow us to read our own target mask.
       Since PPIs are by definition targeted at just "self",
       and the registers are banked for each cpu target,
       we logically start looking from IRQ 16 to get the mask.
    */
    debug_early_entry("gic_driver::init_cpu()");

    assert(smp_idx < max_cpu_if);
    unsigned char my_mask = 0;

    for (int i = 16; i < 32; i++) { /* check PPIs target */
        my_mask = this->gicd.read_reg_grp(gicd_reg_irq8::GICD_ITARGETSR, i);
        if (my_mask) {
            this->cpu_targets[smp_idx] = my_mask;
            break;
        }
    }

    if (!my_mask) {
        abort("gic: failed to read cpu mask");
    }

    /* disable all PPI interrupts */
    this->gicd.write_reg_raw((u32)gicd_reg_irq1::GICD_ICENABLER, 0,
                             0xffff0000);
    /* enable all SGI interrupts */
    this->gicd.write_reg_raw((u32)gicd_reg_irq1::GICD_ISENABLER, 0,
                             0x0000ffff);
    /* set priority on SGI/PPI (at least bits [7:4] must be implemented) */
    for (int i = 0; i < 32; i += 4) {
        this->gicd.write_reg_raw((u32)gicd_reg_irq8::GICD_IPRIORITYR, i,
                                 0xc0c0c0c0);
    }

    /* set priority mask register for CPU */
    for (int i = 0; i < 32; i += 4) {
        this->gicc.write_reg(gicc_reg::GICC_PMR, 0xf0);
    }

    /* enable CPU interface */
    unsigned int gicc_ctlr = this->gicc.read_reg(gicc_reg::GICC_CTLR);
    gicc_ctlr |= 1;
    this->gicc.write_reg(gicc_reg::GICC_CTLR, gicc_ctlr);

    debug_early("CPU interface enabled.\n");
}

/* to be called only from the boot CPU */
void gic_driver::init_dist(int smp_idx)
{
    debug_early_entry("gic_driver::init_dist()");

    /* disable first */
    unsigned int gicd_ctlr;
    gicd_ctlr = this->gicd.read_reg(gicd_reg::GICD_CTLR);
    gicd_ctlr &= ~1;
    this->gicd.write_reg(gicd_reg::GICD_CTLR, gicd_ctlr);

    /* note: number of CPU interfaces could be read from here as well */
    this->nr_irqs = ((this->gicd.read_reg(gicd_reg::GICD_TYPER) & 0x1f)
                     + 1) * 32;
    debug_early_u64("number of supported IRQs: ", (u64)this->nr_irqs);

    /* set all SPIs to level-sensitive at the start */
    for (int i = 32; i < this->nr_irqs; i += 16)
        this->gicd.write_reg_raw((u32)gicd_reg_irq2::GICD_ICFGR, i / 4, 0);

    unsigned int mask = this->cpu_targets[smp_idx];
    mask |= mask << 8;  /* duplicate pattern (x2) */
    mask |= mask << 16; /* duplicate pattern (x4) */

    /* send all SPIs to this only, set priority */
    for (int i = 32; i < this->nr_irqs; i += 4) {
        this->gicd.write_reg_raw((u32)gicd_reg_irq8::GICD_ITARGETSR, i,
                                 mask);
        this->gicd.write_reg_raw((u32)gicd_reg_irq8::GICD_IPRIORITYR, i,
                                 0xc0c0c0c0);
    }

    /* disable all SPIs */
    for (int i = 32; i < this->nr_irqs; i += 32)
        this->gicd.write_reg_raw((u32)gicd_reg_irq1::GICD_ICENABLER, i / 8,
                                 0xffffffff);

    /* enable distributor interface */
    gicd_ctlr |= 1;
    this->gicd.write_reg(gicd_reg::GICD_CTLR, gicd_ctlr);
}

void gic_driver::mask_irq(unsigned int id)
{
    WITH_LOCK(gic_lock) {
        this->gicd.write_reg_grp(gicd_reg_irq1::GICD_ICENABLER, id, 1);
    }
}

void gic_driver::unmask_irq(unsigned int id)
{
    WITH_LOCK(gic_lock) {
        this->gicd.write_reg_grp(gicd_reg_irq1::GICD_ISENABLER, id, 1);
    }
}

void gic_driver::set_irq_type(unsigned int id, irq_type type)
{
    WITH_LOCK(gic_lock) {
        this->gicd.write_reg_grp(gicd_reg_irq2::GICD_ICFGR, id, (u32)type << 1);
    }
}

unsigned int gic_driver::ack_irq(void)
{
    unsigned int iar;
    iar = this->gicc.read_reg(gicc_reg::GICC_IAR);
    return iar;
}

void gic_driver::end_irq(unsigned int iar)
{
    this->gicc.write_reg(gicc_reg::GICC_EOIR, iar);
}

unsigned int gic_dist::read_reg(gicd_reg reg)
{
    return mmio_getl((mmioaddr_t)this->base + (u32)reg);
}

void gic_dist::write_reg(gicd_reg reg, unsigned int value)
{
    mmio_setl((mmioaddr_t)this->base + (u32)reg, value);
}

unsigned int gic_dist::read_reg_raw(unsigned int reg, unsigned int offset)
{
    return mmio_getl((mmioaddr_t)this->base + (u32)reg + offset);
}

void gic_dist::write_reg_raw(unsigned int reg, unsigned int offset,
                             unsigned int value)
{
    mmio_setl((mmioaddr_t)this->base + (u32)reg + offset, value);
}

unsigned char gic_dist::read_reg_grp(gicd_reg_irq1 reg, unsigned int irq)
{
    assert(irq < max_nr_irqs);

    unsigned int offset = (u32)reg + (irq / 32) * 4;
    unsigned int mask = 1 << (irq % 32);

    return (mmio_getl((mmioaddr_t)this->base + offset) & mask) ? 1 : 0;
}

void gic_dist::write_reg_grp(gicd_reg_irq1 reg, unsigned int irq,
                             unsigned char value)
{
    assert(value <= 1 && irq < max_nr_irqs);

    unsigned int offset = (u32)reg + (irq / 32) * 4;
    unsigned int shift = irq % 32;
    unsigned int old = mmio_getl((mmioaddr_t)this->base + offset);

    old &= ~(1 << shift);
    old |= value << shift;

    mmio_setl((mmioaddr_t)this->base + offset, old);
}

unsigned char gic_dist::read_reg_grp(gicd_reg_irq2 reg, unsigned int irq)
{
    assert(irq < max_nr_irqs);

    unsigned int offset = (u32)reg + (irq / 16) * 4;
    unsigned int shift = ((irq % 16) * 2);
    unsigned int old = mmio_getl((mmioaddr_t)this->base + offset);

    return (old >> shift) & 0x3;
}

void gic_dist::write_reg_grp(gicd_reg_irq2 reg, unsigned int irq,
                             unsigned char value)
{
    assert(value <= 3 && irq < max_nr_irqs);

    unsigned int offset = (u32)reg + (irq / 16) * 4;
    unsigned int shift = ((irq % 16) * 2);
    unsigned int old = mmio_getl((mmioaddr_t)this->base + offset);

    old &= ~(0x3 << shift);
    old |= value << shift;

    mmio_setl((mmioaddr_t)this->base + offset, old);
}

/* spec explicitly mentions that this class of registers is byte-accessible */
unsigned char gic_dist::read_reg_grp(gicd_reg_irq8 reg, unsigned int irq)
{
    assert(irq < max_nr_irqs);

    unsigned int offset = (u32)reg + irq;
    return mmio_getb((mmioaddr_t)this->base + offset);
}

void gic_dist::write_reg_grp(gicd_reg_irq8 reg, unsigned int irq,
                             unsigned char value)
{
    assert(irq < max_nr_irqs);

    unsigned int offset = (u32)reg + irq;
    mmio_setb((mmioaddr_t)this->base + offset, value);
}

unsigned int gic_cpu::read_reg(gicc_reg r)
{
    return mmio_getl((mmioaddr_t)this->base + (u32)r);
}

void gic_cpu::write_reg(gicc_reg r, unsigned int value)
{
    mmio_setl((mmioaddr_t)this->base + (u32)r, value);
}

}
