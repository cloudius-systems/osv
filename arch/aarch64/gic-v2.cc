/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mmio.hh>
#include <osv/irqlock.hh>
#include <osv/kernel_config_logger_debug.h>

#include "processor.hh"
#include "gic-v2.hh"
#include "arm-clock.hh"

namespace gic {

void gic_v2_dist::disable()
{
    unsigned int gicd_ctlr = read_reg(gicd_reg::GICD_CTLR);
    gicd_ctlr &= ~1;
    write_reg(gicd_reg::GICD_CTLR, gicd_ctlr);
}

void gic_v2_dist::enable()
{
    unsigned int gicd_ctlr = read_reg(gicd_reg::GICD_CTLR);
    gicd_ctlr |= 1;
    write_reg(gicd_reg::GICD_CTLR, gicd_ctlr);
}

void gic_v2_dist::write_reg_grp(gicd_reg_irq1 reg, unsigned int irq, u8 value)
{
    assert(value <= 1 && irq < max_nr_irqs);

    unsigned int offset = (u32)reg + (irq / 32) * 4;
    unsigned int shift = irq % 32;
    unsigned int old = mmio_getl((mmioaddr_t)_base + offset);

    old &= ~(1 << shift);
    old |= value << shift;

    mmio_setl((mmioaddr_t)_base + offset, old);
}

void gic_v2_dist::write_reg_grp(gicd_reg_irq2 reg, unsigned int irq, u8 value)
{
    assert(value <= 3 && irq < max_nr_irqs);

    unsigned int offset = (u32)reg + (irq / 16) * 4;
    unsigned int shift = ((irq % 16) * 2);
    unsigned int old = mmio_getl((mmioaddr_t)_base + offset);

    old &= ~(0x3 << shift);
    old |= value << shift;

    mmio_setl((mmioaddr_t)_base + offset, old);
}

u32 gic_v2_cpu::read_reg(gicc_reg reg)
{
    return mmio_getl((mmioaddr_t)_base + (u32)reg);
}

void gic_v2_cpu::write_reg(gicc_reg reg, u32 value)
{
    mmio_setl((mmioaddr_t)_base + (u32)reg, value);
}

void gic_v2_cpu::enable()
{
    unsigned int gicc_ctlr = read_reg(gicc_reg::GICC_CTLR);
    gicc_ctlr |= 1;
    write_reg(gicc_reg::GICC_CTLR, gicc_ctlr);
}

/* to be called only from the boot CPU */
void gic_v2_driver::init_dist()
{
    _gicd.disable();

    _nr_irqs = _gicd.read_number_of_interrupts();
    if (_nr_irqs > GIC_MAX_IRQ) {
        _nr_irqs = GIC_MAX_IRQ + 1;
    }

    // Send all SPIs to the cpu 0
    u32 cpu_0_mask = 1U;
    cpu_0_mask |= cpu_0_mask << 8;  /* duplicate pattern (x2) */
    cpu_0_mask |= cpu_0_mask << 16; /* duplicate pattern (x4) */
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += 4) {
        _gicd.write_reg_at_offset((u32)gicd_reg_irq8::GICD_ITARGETSR, i, cpu_0_mask);
    }

    // Set all SPIs to level-sensitive at the start
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += 16)
        _gicd.write_reg_at_offset((u32)gicd_reg_irq2::GICD_ICFGR, i / 4, GICD_ICFGR_DEF_TYPE);

    // Set priority
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += 4) {
        _gicd.write_reg_at_offset((u32)gicd_reg_irq8::GICD_IPRIORITYR, i, GICD_IPRIORITY_DEF);
    }

    // Deactivate and disable all SPIs
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += 32) {
        _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ICACTIVER, i / 8, GICD_DEF_ICACTIVERn);
        _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ICENABLER, i / 8, GICD_DEF_ICENABLERn);
    }

    _gicd.enable();
}

void gic_v2_driver::init_cpuif(int smp_idx)
{
#if CONF_logger_debug
    debug_early_entry("gic_driver::init_cpuif()");
#endif

    /* set priority mask register for CPU */
    _gicc.write_reg(gicc_reg::GICC_PMR, 0xf0);

    /* disable all PPI interrupts */
    _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ICENABLER, 0, 0xffff0000);

    /* enable all SGI interrupts */
    _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ISENABLER, 0, 0x0000ffff);

    /* set priority on SGI/PPI (at least bits [7:4] must be implemented) */
    for (int i = 0; i < GIC_SPI_BASE; i += 4) {
        _gicd.write_reg_at_offset((u32)gicd_reg_irq8::GICD_IPRIORITYR, i, GICD_IPRIORITY_DEF);
    }

    /* enable CPU interface */
    _gicc.enable();

    /* enable CPU clock timer PPI interrupt on non-primary CPUs */
    if (smp_idx) {
        _gicd.write_reg_grp(gicd_reg_irq1::GICD_ISENABLER, get_timer_irq_id(), 1);
    }

#if CONF_logger_debug
    debug_early("CPU interface enabled.\n");
#endif
}

void gic_v2_driver::mask_irq(unsigned int id)
{
    WITH_LOCK(gic_lock) {
        _gicd.write_reg_grp(gicd_reg_irq1::GICD_ICENABLER, id, 1);
    }
}

void gic_v2_driver::unmask_irq(unsigned int id)
{
    WITH_LOCK(gic_lock) {
        _gicd.write_reg_grp(gicd_reg_irq1::GICD_ISENABLER, id, 1);
    }
}

void gic_v2_driver::set_irq_type(unsigned int id, irq_type type)
{
    WITH_LOCK(gic_lock) {
        _gicd.write_reg_grp(gicd_reg_irq2::GICD_ICFGR, id, (u32)type << 1);
    }
}

/* send software-generated interrupt to other cpus; vector is [0..15]
 * GICD_SGIR distributor register:
 *
 * 31        26 | 25  24 | 23          16 | 15    | 14       4 | 3     0
 *   reserved   | filter |    cpulist     | NSATT |  reserved  |  INTID
 */
void gic_v2_driver::send_sgi(sgi_filter filter, int smp_idx, unsigned int vector)
{
    u32 sgir = 0;
    assert(vector <= 0x0f);
    irq_save_lock_type irq_lock;

    //We disable interrupts before taking a lock to prevent scenarios
    //when interrupt arrives after gic_lock is taken and interrupt handler
    //ends up calling send_sgi() (nested example) and stays spinning forever
    //in attempt to take a lock again
    WITH_LOCK(irq_lock) {
        WITH_LOCK(gic_lock) {
            switch (filter) {
            case sgi_filter::SGI_TARGET_LIST:
                sgir = 1U << (((u32)smp_idx) + 16u);
                break;
            case sgi_filter::SGI_TARGET_ALL_BUT_SELF:
                sgir = 1 << 24u;
                break;
            case sgi_filter::SGI_TARGET_SELF:
                sgir = 2 << 24u;
                break;
            }
            asm volatile ("dmb ishst");
            _gicd.write_reg(gicd_reg::GICD_SGIR, sgir | vector);
        }
    }
}

unsigned int gic_v2_driver::ack_irq(void)
{
    return _gicc.read_reg(gicc_reg::GICC_IAR);
}

void gic_v2_driver::end_irq(unsigned int iar)
{
    _gicc.write_reg(gicc_reg::GICC_EOIR, iar);
}

}
