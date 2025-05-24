/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * The code below is losely based on the implementation of GICv3
 * in the Unikraft project
 * (see https://github.com/unikraft/unikraft/blob/staging/drivers/ukintctlr/gic/gic-v3.c)
 * -----------------------------------------------------------
 * Copyright (c) 2020, OpenSynergy GmbH. All rights reserved.
 *
 * ARM Generic Interrupt Controller support v3 version
 * based on plat/drivers/gic/gic-v2.c:
 *
 * Authors: Wei Chen <Wei.Chen@arm.com>
 *          Jianyong Wu <Jianyong.Wu@arm.com>
 *
 * Copyright (c) 2018, Arm Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holder nor the names of its
 *    contributors may be used to endorse or promote products derived from
 *    this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <osv/mmio.hh>
#include <osv/sched.hh>
#include <osv/contiguous_alloc.hh>
#include <osv/ilog2.hh>
#include <osv/mmu.hh>
#include <drivers/pci-function.hh>

#include <algorithm>

#include "processor.hh"
#include "gic-v3.hh"
#include "arm-clock.hh"

extern class interrupt_table idt;

#define isb() ({ asm volatile ("isb"); })

namespace gic {

void gic_v3_dist::wait_for_write_complete()
{
    unsigned int val;

    do {
        val = read_reg(gicd_reg::GICD_CTLR);
    } while (val & GICD_CTLR_WRITE_COMPLETE);
}

void gic_v3_dist::disable()
{
    write_reg(gicd_reg::GICD_CTLR, 0);
    wait_for_write_complete();
}

void gic_v3_dist::enable()
{
    write_reg(gicd_reg::GICD_CTLR, GICD_CTLR_ARE_NS |
                        GICD_CTLR_ENABLE_G0 | GICD_CTLR_ENABLE_G1NS);
    wait_for_write_complete();
}

gic_v3_redist::gic_v3_redist(mmu::phys b, size_t l) : _base(b)
{
    mmu::linear_map((void *)_base, _base, l, "gic_redist", mmu::page_size, mmu::mattr::dev);
}

void gic_v3_redist::init_cpu_base(int smp_idx)
{
    if (!smp_idx) {
        _cpu_bases = new mmu::phys[sched::cpus.size()];
    }

    uint64_t mpidr = processor::read_mpidr();

    u64 offset = 0;
    u64 typer;
    do {
        typer = mmio_getq((mmioaddr_t)_base + (offset + GICR_TYPER));

        if (((mpidr & MPIDR_AFF3_MASK) >> 32) == GICR_TYPER_AFF3(typer) &&
            ((mpidr & MPIDR_AFF2_MASK) >> 16) == GICR_TYPER_AFF2(typer) &&
            ((mpidr & MPIDR_AFF1_MASK) >> 8) == GICR_TYPER_AFF1(typer) &&
             (mpidr & MPIDR_AFF0_MASK) == GICR_TYPER_AFF0(typer)) {
            break;
        }
        offset += GICR_STRIDE;
        if (typer & GICR_TYPER_VLPIS) {
            offset += GICR_STRIDE;
        }
    } while (!(typer & GICR_TYPER_LAST));

    _cpu_bases[smp_idx] = _base + offset;
}

void gic_v3_redist::init_lpis(int smp_idx, u64 prop_base, u64 pend_base)
{
    //Set common LPI configuration table
    write64_at_offset(smp_idx, GICR_PROPBASER, prop_base);
    //Set redistributor specific LPI pending table
    write64_at_offset(smp_idx, GICR_PENDBASER, pend_base);
    //Enable LPIs
    write_at_offset(smp_idx, GICR_CTLR, GICR_CTLR_EnableLPIs);
}

u32 gic_v3_redist::read_at_offset(int smp_idx, u32 offset)
{
    return mmio_getl((mmioaddr_t)_cpu_bases[smp_idx] + offset);
}

u64 gic_v3_redist::read64_at_offset(int smp_idx, u32 offset)
{
    return mmio_getq((mmioaddr_t)_cpu_bases[smp_idx] + offset);
}

void gic_v3_redist::write_at_offset(int smp_idx, u32 offset, u32 value)
{
    mmio_setl((mmioaddr_t)_cpu_bases[smp_idx] + offset, value);
}

void gic_v3_redist::write64_at_offset(int smp_idx, u32 offset, u64 value)
{
    mmio_setq((mmioaddr_t)_cpu_bases[smp_idx] + offset, value);
}

void gic_v3_redist::wait_for_write_complete()
{
    unsigned int val;

    do {
        val = mmio_getl((mmioaddr_t)_base);
    } while (val & GICD_CTLR_WRITE_COMPLETE);
}

void gic_v3_redist::init_rdbase(int smp_idx, bool pta)
{
    if (!smp_idx) {
        _rdbases = new mmu::phys[sched::cpus.size()];
    }

    if (pta) {
        _rdbases[smp_idx] = (_cpu_bases[smp_idx]) >> 16;
    } else {
        u64 typer = read64_at_offset(smp_idx, GICR_TYPER);
        _rdbases[smp_idx] = GICR_TYPER_PROC_NUM(typer);
    }
}

static uint32_t get_cpu_affinity(void)
{
    uint64_t mpidr = processor::read_mpidr();

    uint64_t aff = ((mpidr & MPIDR_AFF3_MASK) >> 8) |
        (mpidr & MPIDR_AFF2_MASK) |
        (mpidr & MPIDR_AFF1_MASK) |
        (mpidr & MPIDR_AFF0_MASK);

    return (uint32_t)aff;
}

gic_v3_its::gic_v3_its(mmu::phys b, size_t l) : _base(b)
{
    if (b && l) {
        mmu::linear_map((void *)_base, _base, l, "gic_its", mmu::page_size,
                        mmu::mattr::dev);
    }
}

u64 gic_v3_its::read_reg64(gic_its_reg reg)
{
    return mmio_getq((mmioaddr_t)_base + (u32)reg);
}

u64 gic_v3_its::read_reg64_at_offset(gic_its_reg reg, u32 offset)
{
    return mmio_getq((mmioaddr_t)_base + (u32)reg + offset);
}

void gic_v3_its::write_reg(gic_its_reg reg, u32 value)
{
    mmio_setl((mmioaddr_t)_base + (u32)reg, value);
}

void gic_v3_its::write_reg64(gic_its_reg reg, u64 value)
{
    mmio_setq((mmioaddr_t)_base + (u32)reg, value);
}

void gic_v3_its::write_reg64_at_offset(gic_its_reg reg, u32 offset, u64 value)
{
    mmio_setq((mmioaddr_t)_base + (u32)reg + offset, value);
}

void gic_v3_its::read_type_register()
{
    _typer = read_reg64(gic_its_reg::GICITS_TYPER);
}

//The 4K queue is enough for 128 commands before it wraps around
#define GIC_ITS_CMD_QUEUE_SIZE  0x1000 //4 KB
//https://developer.arm.com/documentation/102923/0100/ITS/The-command-queue
void gic_v3_its::initialize_cmd_queue()
{
    //Queue needs to be 64KB aligned
    _cmd_queue = memory::alloc_phys_contiguous_aligned(GIC_ITS_CMD_QUEUE_SIZE, 0x10000);
    memset(_cmd_queue, 0, GIC_ITS_CMD_QUEUE_SIZE);

    u64 cmd_queue_pa = mmu::virt_to_phys(_cmd_queue);
    u64 queue_size_in_pages = GIC_ITS_CMD_QUEUE_SIZE / mmu::page_size;
    //
    //Read https://developer.arm.com/documentation/ddi0601/2024-09/External-Registers/GITS-CBASER--ITS-Command-Queue-Descriptor
    write_reg64(gic_its_reg::GICITS_CBASER, GITS_CBASER_VALID | cmd_queue_pa | (queue_size_in_pages - 1));
    write_reg64(gic_its_reg::GICITS_CWRITER, 0);
}

void gic_v3_its::enqueue_cmd(its_cmd *cmd)
{
    u64 cread = read_reg64(gic_its_reg::GICITS_CREADR);
    u64 cwrite = read_reg64(gic_its_reg::GICITS_CWRITER);
    //
    //Wait until queue is not full
    while (cread == cwrite + sizeof(*cmd)) {
        asm volatile ("isb sy");
        cread = read_reg64(gic_its_reg::GICITS_CREADR);
    }

    its_cmd *cmd_to_write = (its_cmd *)(_cmd_queue + cwrite);
    *cmd_to_write = *cmd;

    cwrite += sizeof(*cmd);
    if (cwrite == GIC_ITS_CMD_QUEUE_SIZE) {
        cwrite = 0;
    }

    write_reg64(gic_its_reg::GICITS_CWRITER, cwrite);
}

#define CPUID_2_ICID(cpuId) (cpuId)

//See 6.3.9 in GIC3/4 spec
//"Maps the Device table entry associated with DeviceID to its associated ITT,
// defined by itt_pa and itt_size."
void gic_v3_its::cmd_mapd(u32 dev_id, u64 itt_pa, u64 itt_size)
{
    its_cmd cmd;
    cmd.data[0] = ((u64)dev_id << 32) | (u32)gic_its_cmd::ITS_CMD_MAPD;
    cmd.data[1] = itt_size;
    cmd.data[2] = ITS_MAPD_V | itt_pa;
    cmd.data[3] = 0;
    enqueue_cmd(&cmd);
}

//See 6.3.11 in GIC3/4 spec
//"Maps the event defined by EventID and DeviceID to its associated ITE, defined by ICID and pINTID in
// the ITT associated with DeviceID"
void gic_v3_its::cmd_mapti(u32 dev_id, int vector, int smp_idx)
{
    its_cmd cmd;
    cmd.data[0] = ((u64)dev_id << 32) | (u32)gic_its_cmd::ITS_CMD_MAPTI;
    u32 event_id = vector - GIC_LPI_INTS_START;
    cmd.data[1] = ((u64)vector << 32) | event_id;
    cmd.data[2] = CPUID_2_ICID(smp_idx);
    cmd.data[3] = 0;
    enqueue_cmd(&cmd);
}

//See 6.3.13 in GIC3/4 spec
//"Updates the ICID field in the ITT entry for the event defined by DeviceID and EventID."
void gic_v3_its::cmd_movi(u32 dev_id, int vector, int smp_idx)
{
    its_cmd cmd;
    cmd.data[0] = ((u64)dev_id << 32) | (u32)gic_its_cmd::ITS_CMD_MOVI;
    u32 event_id = vector - GIC_LPI_INTS_START;
    cmd.data[1] = event_id;
    cmd.data[2] = CPUID_2_ICID(smp_idx);
    cmd.data[3] = 0;
    enqueue_cmd(&cmd);
}

//See 6.3.6 in GIC3/4 spec
//"Specifies that the ITS must ensure that any caching in the Redistributors associated with the specified
// EventID is consistent with the LPI Configuration tables held in memory."
void gic_v3_its::cmd_inv(u32 dev_id, int vector)
{
    its_cmd cmd;
    cmd.data[0] = ((u64)dev_id << 32) | (u32)gic_its_cmd::ITS_CMD_INV;
    u32 event_id = vector - GIC_LPI_INTS_START;
    cmd.data[1] = event_id;
    cmd.data[2] = cmd.data[3] = 0;
    enqueue_cmd(&cmd);
}

//See 6.3.4 in GIC3/4 spec
//"Instructs the appropriate Redistributor to remove the pending state of the interrupt."
void gic_v3_its::cmd_discard(u32 dev_id, int vector)
{
    its_cmd cmd;
    cmd.data[0] = ((u64)dev_id << 32) | (u32)gic_its_cmd::ITS_CMD_DISCARD;
    u32 event_id = vector - GIC_LPI_INTS_START;
    cmd.data[1] = event_id;
    cmd.data[2] = cmd.data[3] = 0;
    enqueue_cmd(&cmd);
}

//See 6.3.14 in GIC3/4 spec
//"Ensures all outstanding ITS operations associated with physical interrupts for the Redistributor
// specified by RDbase are globally observed before any further ITS commands are executed."
void gic_v3_its::cmd_sync(mmu::phys rdbase)
{
    its_cmd cmd;
    cmd.data[0] = (u64)gic_its_cmd::ITS_CMD_SYNC;
    cmd.data[2] = rdbase << 16;
    cmd.data[1] = cmd.data[3] = 0;
    enqueue_cmd(&cmd);
}

//See 6.3.8 in GIC3/4 spec
//"Maps the Collection table entry defined by ICID to the target Redistributor, defined by RDbase"
void gic_v3_its::cmd_mapc(int smp_idx, mmu::phys rdbase)
{
    its_cmd cmd;
    cmd.data[0] = (u32)gic_its_cmd::ITS_CMD_MAPC;
    cmd.data[1] = 0;
    cmd.data[2] = ITS_MAPC_V | (rdbase << 16) | CPUID_2_ICID(smp_idx);
    cmd.data[3] = 0;
    enqueue_cmd(&cmd);
}

void gic_v3_driver::init_lpis(int smp_idx)
{
    //Check if ITS is supported which is for example not a case on Firecracker
    if (!_gits.base()) {
        return;
    }

    //Identify number of LPIs supported by GIC and setup global configuration table
    if (smp_idx == 0) {
        //See https://developer.arm.com/documentation/ddi0601/2022-06/External-Registers/GICD-TYPER--Interrupt-Controller-Type-Register?lang=en
        //Read bits 15:11 (num_LPIs) of GICD_TYPER
        u32 typer = _gicd.read_reg(gicd_reg::GICD_TYPER);
        u32 num_lpis = (typer >> 11) & GICD_TYPER_LPI_NUM_MASK;
        if (num_lpis) {
            _msi_vector_num = 1UL << (num_lpis + 1);
        } else { //Determine using the IDBits field
            u32 id_bits = (typer >> 19) & GICD_TYPER_IDBITS_MASK;
            _msi_vector_num = (1UL << (id_bits + 1)) - GIC_LPI_INTS_START;
        }
        //TODO: Investigate using smaller number of LPIs using GICR_PROPBASER.IDbits
        //Read https://developer.arm.com/documentation/102923/0100/Redistributors/Initial-configuration-of-a-Redistributor
        //and https://developer.arm.com/documentation/ddi0601/2024-09/External-Registers/GICR-PROPBASER--Redistributor-Properties-Base-Address-Register
        _msi_vector_num = std::max(_msi_vector_num, (u16)4096);

        //Allocate common LPI configuration table
        void *config_table = memory::alloc_phys_contiguous_aligned(_msi_vector_num, 4096);
        memset(config_table, 0, _msi_vector_num);
        _lpi_config_table = (u8*)config_table;

        u64 id_bits = ilog2_roundup<u64>(_msi_vector_num + GIC_LPI_INTS_START) - 1;
        _lpi_prop_base = mmu::virt_to_phys(config_table) | id_bits;

        //Allocate LPI pending table for each redistributor
        //From https://developer.arm.com/documentation/102923/0100/Redistributors:
        //"Each Redistributor has its own LPI Pending table, and these tables are not shared between Redistributors."
        _lpi_pend_bases = new u64[sched::cpus.size()];
        size_t pending_table_size = (_msi_vector_num + GIC_LPI_INTS_START) / 8;
        for (unsigned c = 0; c < sched::cpus.size(); c++) {
            void *pending_table = memory::alloc_phys_contiguous_aligned(pending_table_size, 64 * 1024);
            memset(pending_table, 0, pending_table_size);
            //Read about PTZ here - https://developer.arm.com/documentation/ddi0601/2024-12/External-Registers/GICR-PENDBASER--Redistributor-LPI-Pending-Table-Base-Address-Register
            _lpi_pend_bases[c] = mmu::virt_to_phys(pending_table) | GICR_PENDBASER_PTZ;
        }
    }

    //Set LPI configuration, pending table for each redistributor
    _gicrd.init_lpis(smp_idx, _lpi_prop_base, _lpi_pend_bases[smp_idx]);
}

/* to be called only from the boot CPU */
void gic_v3_driver::init_dist()
{
    _gicd.disable();

    _nr_irqs = _gicd.read_number_of_interrupts();
    if (_nr_irqs > GIC_MAX_IRQ) {
        _nr_irqs = GIC_MAX_IRQ + 1;
    }

    /* Configure all SPIs as non-secure Group 1 */
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += GICD_I_PER_IGROUPRn)
        _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_IGROUPR, 4 * (i >> 5), GICD_DEF_IGROUPRn);

    // Send all SPIs to this cpu
    u64 aff = (uint64_t)get_cpu_affinity();
    u64 irouter_val = GIC_AFF_TO_ROUTER(aff, 0);

    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i++)
        _gicd.write_reg64_at_offset(GICD_IROUTER_BASE, i * 8, irouter_val);

    //
    // Set all SPIs to level-sensitive at the start
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += GICD_I_PER_ICFGRn)
        _gicd.write_reg_at_offset((u32)gicd_reg_irq2::GICD_ICFGR, i / 4, GICD_ICFGR_DEF_TYPE);

    // Set priority
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += GICD_I_PER_IPRIORITYn) {
        _gicd.write_reg_at_offset((u32)gicd_reg_irq8::GICD_IPRIORITYR, i, GICD_IPRIORITY_DEF);
    }

    // Deactivate and disable all SPIs
    for (unsigned int i = GIC_SPI_BASE; i < _nr_irqs; i += GICD_I_PER_ICACTIVERn) {
        _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ICACTIVER, i / 8, GICD_DEF_ICACTIVERn);
        _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ICENABLER, i / 8, GICD_DEF_ICENABLERn);
    }

    _gicd.wait_for_write_complete();

    _gicd.enable();
}

#define __STRINGIFY(x) #x
#define READ_SYS_REG32(reg)                                       ({ \
    u32 v;                                                           \
    asm volatile("mrs %0, " __STRINGIFY(reg) : "=r"(v) :: "memory"); \
    v;                                                               \
})

#define WRITE_SYS_REG32(reg, v)                                            ({ \
    asm volatile("msr " __STRINGIFY(reg) ", %0" :: "r"((u32)(v)) : "memory"); \
})

#define READ_SYS_REG64(reg)                                       ({ \
    u64 v;                                                           \
    asm volatile("mrs %0, " __STRINGIFY(reg) : "=r"(v) :: "memory"); \
    v;                                                               \
})

#define WRITE_SYS_REG64(reg, v)                                            ({ \
    asm volatile("msr " __STRINGIFY(reg) ", %0" :: "r"((u64)(v)) : "memory"); \
})

void gic_v3_driver::init_redist(int smp_idx)
{
    //Grab current cpu mpid and store it in the array
    _mpids_by_smpid[smp_idx] = processor::read_mpidr();

    /* Wake up CPU redistributor */
    u32 val = _gicrd.read_at_offset(smp_idx, GICR_WAKER);
    val &= ~GICR_WAKER_ProcessorSleep;
    _gicrd.write_at_offset(smp_idx, GICR_WAKER, val);

    /* Poll GICR_WAKER.ChildrenAsleep */
    do {
        val = _gicrd.read_at_offset(smp_idx, GICR_WAKER);
    } while ((val & GICR_WAKER_ChildrenAsleep));

    /* Set PPI and SGI to a default value */
    for (unsigned int i = 0; i < GIC_SPI_BASE; i += GICD_I_PER_IPRIORITYn)
        _gicrd.write_at_offset(smp_idx, GICR_IPRIORITYR4(i), GICD_IPRIORITY_DEF);

    /* Deactivate SGIs and PPIs as the state is unknown at boot */
    _gicrd.write_at_offset(smp_idx, GICR_ICACTIVER0, GICD_DEF_ICACTIVERn);

    /* Disable all PPIs */
    _gicrd.write_at_offset(smp_idx, GICR_ICENABLER0, GICD_DEF_PPI_ICENABLERn);

    /* Configure SGIs and PPIs as non-secure Group 1 */
    _gicrd.write_at_offset(smp_idx, GICR_IGROUPR0, GICD_DEF_IGROUPRn);

    /* Enable all SGIs */
    _gicrd.write_at_offset(smp_idx, GICR_ISENABLER0, GICD_DEF_SGI_ISENABLERn);

    /* Wait for completion */
    _gicrd.wait_for_write_complete();

    /* Enable system register access */
    val = READ_SYS_REG32(ICC_SRE_EL1);
    val |= 0x7;
    WRITE_SYS_REG32(ICC_SRE_EL1, val);
    isb();

    /* No priority grouping */
    WRITE_SYS_REG32(ICC_BPR1_EL1, 0);

    /* Set priority mask register */
    WRITE_SYS_REG32(ICC_PMR_EL1, 0xff);

    /* EOI drops priority, DIR deactivates the interrupt (mode 1) */
    WRITE_SYS_REG32(ICC_CTLR_EL1, GICC_CTLR_EL1_EOImode_drop);

    /* Enable Group 1 interrupts */
    WRITE_SYS_REG32(ICC_IGRPEN1_EL1, 1);

    isb();

    //Enable cpu timer on secondary CPU
    if (smp_idx) {
        u32 val = 1UL << (get_timer_irq_id() % GICR_I_PER_ISENABLERn);
        _gicrd.write_at_offset(smp_idx, GICR_ISENABLER0, val);
    }

    if (!smp_idx) {
        idt.init_msi_vector_base(GIC_LPI_INTS_START);
        idt.set_max_msi_vector(GIC_LPI_INTS_START + _msi_vector_num - 1);
    }
}

//https://developer.arm.com/documentation/102923/0100/ITS/The-sizes-and-layout-of-Collection-and-Device-tables
//"The location and size of the Collection and Device tables is configured
// using the GITS_BASERn registers. Software must allocate memory for
// these tables and configure the GITS_BASERn registers before enabling the ITS."
void gic_v3_driver::init_its_device_or_collection_table(int idx)
{
    //Read https://developer.arm.com/documentation/ddi0601/2024-09/External-Registers/GITS-BASER-n---ITS-Table-Descriptors
    u32 offset = idx * 8;
    u64 base = _gits.read_reg64_at_offset(gic_its_reg::GICITS_BASER, offset);

    u64 type = GITS_TABLE_TYPE(base); //Bits [58:56]
    if (type != GITS_TABLE_DEVICES_TYPE && type != GITS_TABLE_COLLECTIONS_TYPE) {
        return;
    }

    //
    //"Software can allocate a flat (single level) table or two-level tables."
    //We allocate a flat table
    u64 page_size_type = GITS_PAGE_SIZE(base); //Bits [9:8]
    u64 table_size = page_size_type == GITS_TABLE_PAGE_SIZE_4K ? 0x1000 :
	           (page_size_type == GITS_TABLE_PAGE_SIZE_16K ? 0x4000 : 0x10000);

    //if (type == GITS_TABLE_DEVICES_TYPE) {
    //    //TODO: Calculate maximum devices count and save it somewhere
    //}

    void *table = memory::alloc_phys_contiguous_aligned(table_size, table_size);
    memset(table, 0, table_size);

    u64 table_pa = mmu::virt_to_phys(table);
    base = (base & ~GITS_TABLE_BASE_PA_MASK) | table_pa;
    _gits.write_reg64_at_offset(gic_its_reg::GICITS_BASER, offset, GITS_BASER_VALID | base);
}

//https://developer.arm.com/documentation/102923/0100/ITS/Initial-configuration-of-an-ITS
void gic_v3_driver::init_its(int smp_idx)
{
    //Check if ITS is supported which is for example not a case on Firecracker
    if (!_gits.base()) {
        return;
    }

    if (smp_idx == 0) {
        _gits.read_type_register();

        //Initialize the Device and Collection tables
        for (int table_idx = 0; table_idx < GITS_TABLE_NUM_MAX; table_idx++) {
            init_its_device_or_collection_table(table_idx);
        }

        //Initialize command queue
        _gits.initialize_cmd_queue();

        // Enable ITS
        _gits.write_reg(gic_its_reg::GICITS_CTLR, GITS_CTLR_ENABLED);
    }

    //Init on each cpu
    _gicrd.init_rdbase(smp_idx, _gits.is_typer_pta());
    mmu::phys rdbase = _gicrd.rdbase(smp_idx);

    if (smp_idx == 0) {
        // Init on primary CPU
        _gits.cmd_mapc(smp_idx, rdbase);
    } else {
        //Init on secondary cpu
        //We may experience race between many secondary CPUs
        //during early SMP boot so let us protect with spinlock
        WITH_IRQ_LOCK(_smp_init_its_lock) {
            _gits.cmd_mapc(smp_idx, rdbase);
        }
    }
}

#define GIC_LPI_ENABLE  0x01
void gic_v3_driver::mask_irq(unsigned int irq)
{
    WITH_IRQ_LOCK(_gic_lock) {
        if (irq >= GIC_LPI_INTS_START) {
            _lpi_config_table[irq - GIC_LPI_INTS_START] |= ~GIC_LPI_ENABLE;
        } else if (irq >= GIC_SPI_BASE) {
            u32 val = 1UL << (irq % GICD_I_PER_ICENABLERn);
            _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ICENABLER, 4 * (irq >> 5), val);
        } else {
            u32 val = 1UL << (irq % GICR_I_PER_ICENABLERn);
            _gicrd.write_at_offset(sched::cpu::current()->id, GICR_ICENABLER0, val);
        }
    }
}

void gic_v3_driver::unmask_irq(unsigned int irq)
{
    WITH_IRQ_LOCK(_gic_lock) {
        if (irq >= GIC_LPI_INTS_START) {
            _lpi_config_table[irq - GIC_LPI_INTS_START] |= GIC_LPI_ENABLE;
        } else if (irq >= GIC_SPI_BASE) {
            u32 val = 1UL << (irq % GICD_I_PER_ISENABLERn);
            _gicd.write_reg_at_offset((u32)gicd_reg_irq1::GICD_ISENABLER, 4 * (irq >> 5), val);
        } else {
            u32 val = 1UL << (irq % GICR_I_PER_ISENABLERn);
            _gicrd.write_at_offset(sched::cpu::current()->id, GICR_ISENABLER0, val);
        }
    }
}

void gic_v3_driver::set_irq_type(unsigned int id, irq_type type)
{
    //SGIs are always treated as edge-triggered so ignore call for these
    if (id < GIC_PPI_BASE) {
        return;
    }

    WITH_IRQ_LOCK(_gic_lock) {
        auto offset = 4 * ((id) >> 4);
        auto val = _gicd.read_reg_at_offset((u32)gicd_reg_irq2::GICD_ICFGR, offset);
        u32 oldmask = (val >> ((id % GICD_I_PER_ICFGRn) * 2)) & GICD_ICFGR_MASK;

        u32 newmask = oldmask;
        if (type == irq_type::IRQ_TYPE_LEVEL) {
            newmask &= ~GICD_ICFGR_TRIG_MASK;
            newmask |= GICD_ICFGR_TRIG_LVL;
        } else if (type == irq_type::IRQ_TYPE_EDGE) {
            newmask &= ~GICD_ICFGR_TRIG_MASK;
            newmask |= GICD_ICFGR_TRIG_EDGE;
        }

        //Check if nothing changed
        if (newmask == oldmask)
            return;

        // Update to new type
        val &= (~(GICD_ICFGR_MASK << (id % GICD_I_PER_ICFGRn) * 2));
        val |= (newmask << (id % GICD_I_PER_ICFGRn) * 2);
        _gicd.write_reg_at_offset((u32)gicd_reg_irq2::GICD_ICFGR, offset, val);
    }
}

void gic_v3_driver::send_sgi(sgi_filter filter, int smp_idx, unsigned int vector)
{
    assert(smp_idx < max_sgi_cpus);
    assert(vector <= 0x0f);

    //Set vector number in the bits [27:24] - 16 possible values
    u64 sgi_register = vector << ICC_SGIxR_EL1_INTID_SHIFT;

    if (filter == sgi_filter::SGI_TARGET_ALL_BUT_SELF) {
        sgi_register |= ICC_SGIxR_EL1_IRM;
    } else {
        if (filter == sgi_filter::SGI_TARGET_SELF) {
            smp_idx = sched::cpu::current()->id;
        }

        auto mpid = _mpids_by_smpid[smp_idx];
        u64 aff0 = MPIDR_AFF0(mpid);
        sgi_register |= (MPIDR_AFF3(mpid) << ICC_SGIxR_EL1_AFF3_SHIFT) |
                        (MPIDR_AFF2(mpid) << ICC_SGIxR_EL1_AFF2_SHIFT) |
                        (MPIDR_AFF1(mpid) << ICC_SGIxR_EL1_AFF1_SHIFT) |
                        ((aff0 >> 4) << ICC_SGIxR_EL1_RS_SHIFT) | (1 << (aff0 & 0xf));
    }

    /* Generate interrupt */
    WITH_IRQ_LOCK(_gic_lock) {
        WRITE_SYS_REG64(ICC_SGI1R_EL1, sgi_register);
    }
}

unsigned int gic_v3_driver::ack_irq(void)
{
    uint32_t irq;

    irq = READ_SYS_REG32(ICC_IAR1_EL1);
    asm volatile ("dsb sy");

    return irq;
}

void gic_v3_driver::end_irq(unsigned int irq)
{
    /* Lower the priority */
    WRITE_SYS_REG32(ICC_EOIR1_EL1, irq);
    isb();

    /* Deactivate */
    WRITE_SYS_REG32(ICC_DIR_EL1, irq);
    isb();
}

u32 gic_v3_driver::pci_device_id(pci::function* dev)
{
    u8 bus, device, function;
    dev->get_bdf(bus, device, function);
    return (((u32)bus) << 8) | (((u32)device) << 3) | (u32)function;
}

void gic_v3_driver::allocate_msi_dev_mapping(pci::function* dev)
{
    //Read https://developer.arm.com/documentation/102923/0100/ITS/Mapping-an-interrupt-to-a-Redistributor

    //Check if there is an Interrupt Translation Table (ITT) for this device
    //If not create and register it in ITS
    u32 device_id = pci_device_id(dev);

    //Iterate over existing entries to see if one for this device
    //already exists and return; otherwise find an index to where to
    //store new entry
    unsigned itt_index = 0;
    WITH_IRQ_LOCK(_gic_lock) {
        for (; itt_index < max_msi_handlers; itt_index++) {
            if (_itt_by_device_id[itt_index].first == device_id) {
                //We already have an itt entry for this device
                return;
            } else if (!_itt_by_device_id[itt_index].second) {
                //Empty slot - stop
                _itt_by_device_id[itt_index].second = (void*)1; //Mark it reserved
                break;
            }
        }
    }

    //We cannot support more devices than number of vectors limited
    //by max_msi_handlers
    assert(itt_index < max_msi_handlers);

    u64 entries_num = 1ull << ilog2_roundup<u64>(_msi_vector_num);
    u64 itt_size = entries_num * (_gits.itt_entry_size() + 1);
    itt_size = std::max(itt_size, (u64)256);

    //We explicitly allocate memory below to make sure it happens
    //when interrupts are enabled
    void *itt = memory::alloc_phys_contiguous_aligned(itt_size, 256);
    memset(itt, 0, itt_size);

    //Register translation entry in ITS
    u64 itt_pa = mmu::virt_to_phys(itt);
    _irq_lock.lock();
    WITH_LOCK(_gic_lock) {
        _itt_by_device_id[itt_index] = std::make_pair(device_id, itt);
        _gits.cmd_mapd(device_id, itt_pa, ilog2_roundup<u64>(entries_num) - 1);
    }
    _irq_lock.unlock();
}

void gic_v3_driver::map_msi_vector(unsigned int vector, pci::function* dev, u32 target_cpu)
{
    auto index = vector - GIC_LPI_INTS_START;
    assert(index < max_msi_handlers);

    WITH_IRQ_LOCK(_gic_lock) {
        u32 device_id = pci_device_id(dev);

        auto vector_cpu = _cpu_by_vector[index];
        if (!vector_cpu) {
            //Read https://developer.arm.com/documentation/102923/0100/ITS/Mapping-an-interrupt-to-a-Redistributor

            //Map event ID to collection ID|cpu
            _gits.cmd_mapti(device_id, vector, target_cpu);
            _gits.cmd_inv(device_id, vector);

            _cpu_by_vector[index] = target_cpu + 1;

            //Sync redistributor
            mmu::phys rdbase = _gicrd.rdbase(target_cpu);
            _gits.cmd_sync(rdbase);
        } else if ((vector_cpu - 1) != target_cpu) { //We need to move interrupt to different redistributor (cpu)
            //Read https://developer.arm.com/documentation/102923/0100/ITS/Migrating-interrupts-between-Redistributors

            //Re-Map event ID to collection ID|cpu
            _gits.cmd_movi(device_id, vector, target_cpu);
            _gits.cmd_inv(device_id, vector);
            //
            //Sync old redistributor
            mmu::phys rdbase = _gicrd.rdbase(vector_cpu - 1);
            _gits.cmd_sync(rdbase);

            _cpu_by_vector[index] = target_cpu + 1;
        }
    }
}

void gic_v3_driver::unmap_msi_vector(unsigned int vector, pci::function* dev)
{
    auto index = vector - GIC_LPI_INTS_START;
    assert(index < max_msi_handlers);

    WITH_IRQ_LOCK(_gic_lock) {
        auto vector_cpu = _cpu_by_vector[index];
        if (vector_cpu) {
            u32 device_id = pci_device_id(dev);

            _gits.cmd_discard(device_id, vector);
            _gits.cmd_inv(device_id, vector);

            //Sync redistributor
            mmu::phys rdbase = _gicrd.rdbase(vector_cpu - 1);
            _gits.cmd_sync(rdbase);
        }
    }
}

#define GITS_TRANSLATER 0x10040
void gic_v3_driver::msi_format(u64 *address, u32 *data, int vector)
{
    *address = _gits.base() + GITS_TRANSLATER;
    *data = vector - GIC_LPI_INTS_START;
}

}
