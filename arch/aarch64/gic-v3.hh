/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 
 * The code below is losely based on the implementation of GICv3
 * in the Unikraft project
 * (see https://github.com/unikraft/unikraft/blob/staging/drivers/ukintctlr/gic/gic-v3.h)
 * -----------------------------------------------------------
 * Copyright (c) 2020, OpenSynergy GmbH. All rights reserved.
 *
 * ARM Generic Interrupt Controller support v3 version
 * based on plat/drivers/gic/gic-v2.h:
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

#ifndef GIC_V3_HH
#define GIC_V3_HH

#include "gic-common.hh"
#include <unordered_map>
#include <osv/spinlock.h>

#define GICD_CTLR_WRITE_COMPLETE   (1UL << 31)
#define GICD_CTLR_ARE_NS           (1U << 4)
#define GICD_CTLR_ENABLE_G1NS      (1U << 1)
#define GICD_CTLR_ENABLE_G0        (1U << 0)

#define GICD_ICFGR_MASK            0x3
#define GICD_ICFGR_TRIG_LVL        (0 << 1)
#define GICD_ICFGR_TRIG_EDGE       (1 << 1)
#define GICD_ICFGR_TRIG_MASK       0x2
#define GICD_TYPER_LPI_NUM_MASK    0x1f
#define GICD_TYPER_IDBITS_MASK     0x1f

#define GICD_IROUTER_BASE          (0x6000)
#define MPIDR_AFF3_MASK	            0xff00000000
#define MPIDR_AFF2_MASK             0x0000ff0000
#define MPIDR_AFF1_MASK             0x000000ff00
#define MPIDR_AFF0_MASK             0x00000000ff

#define ICC_SGIxR_EL1_AFF3_SHIFT   48
#define ICC_SGIxR_EL1_AFF2_SHIFT   32
#define ICC_SGIxR_EL1_AFF1_SHIFT   16
#define ICC_SGIxR_EL1_RS_SHIFT     44

#define ICC_SGIxR_EL1_IRM          (1UL << 40)

#define MPIDR_AFF3(mpidr)          (((mpidr) >> 32) & 0xff)
#define MPIDR_AFF2(mpidr)          (((mpidr) >> 16) & 0xff)
#define MPIDR_AFF1(mpidr)          (((mpidr) >> 8) & 0xff)
#define MPIDR_AFF0(mpidr)          ((mpidr) & 0xff)

#define GIC_AFF_TO_ROUTER(aff, mode)				\
	((((uint64_t)(aff) << 8) & MPIDR_AFF3_MASK) | ((aff) & 0xffffff) | \
	 ((uint64_t)(mode) << 31))

#define GIC_LPI_INTS_START         8192

#define GICR_STRIDE                (0x20000)
#define GICR_SGI_BASE              (0x10000)

#define GICR_CTLR                  (0x0000)
#define GICR_CTLR_EnableLPIs       (1U)
#define GICR_IIDR                  (0x0004)
#define GICR_TYPER                 (0x0008)
#define GICR_STATUSR               (0x0010)
#define GICR_WAKER                 (0x0014)
#define GICR_SETLPIR               (0x0040)
#define GICR_CLRLPIR               (0x0048)
#define GICR_PROPBASER             (0x0070)
#define GICR_PENDBASER             (0x0078)
#define GICR_INVLPIR               (0x00A0)
#define GICR_INVALLR               (0x00B0)
#define GICR_SYNCR                 (0x00C0)
#define GICR_TYPER_LAST            (0b10000)
#define GICR_TYPER_VLPIS           (0b10)

#define GICR_WAKER_ProcessorSleep  (1U << 1)
#define GICR_WAKER_ChildrenAsleep  (1U << 2)
#define GICR_TYPER_PROC_NUM(type)  (((type) & 0xffff00) >> 8)

#define GICR_IPRIORITYR4(n)        (GICR_SGI_BASE + 0x0400 + 4 * ((n) >> 2))

/* GICR for SGI's & PPI's */
#define GICR_IGROUPR0              (GICR_SGI_BASE + 0x0080)
#define GICR_ISENABLER0            (GICR_SGI_BASE + 0x0100)
#define GICR_ICENABLER0            (GICR_SGI_BASE + 0x0180)
#define GICR_ISPENDR0              (GICR_SGI_BASE + 0x0200)
#define GICR_ICPENDR0              (GICR_SGI_BASE + 0x0280)
#define GICR_ISACTIVER0            (GICR_SGI_BASE + 0x0300)
#define GICR_ICACTIVER0            (GICR_SGI_BASE + 0x0380)
#define GICR_IPRIORITYR0           (GICR_SGI_BASE + 0x0400)
#define GICR_IPRIORITYR7           (GICR_SGI_BASE + 0x041C)
#define GICR_ICFGR0                (GICR_SGI_BASE + 0x0C00)
#define GICR_ICFGR1                (GICR_SGI_BASE + 0x0C04)
#define GICR_IGRPMODR0             (GICR_SGI_BASE + 0x0D00)
#define GICR_NSACR                 (GICR_SGI_BASE + 0x0E00)

#define GICR_TYPER_AFF3(type)      (((type) & 0xff00000000000000) >> 56)
#define GICR_TYPER_AFF2(type)      (((type) & 0x00ff000000000000) >> 48)
#define GICR_TYPER_AFF1(type)      (((type) & 0x0000ff0000000000) >> 40)
#define GICR_TYPER_AFF0(type)      (((type) & 0x000000ff00000000) >> 32)

#define GICD_DEF_PPI_ICENABLERn	   0xffff0000
#define GICD_DEF_SGI_ISENABLERn    0xffff

#define GICC_CTLR_EL1_EOImode_drop (1U << 1)

#define GICR_I_PER_ICENABLERn      32
#define GICR_I_PER_ISENABLERn      32

#define GICR_PENDBASER_PTZ         (1UL << 62)

/*
 * GIC System register assembly aliases
 */
#define ICC_PMR_EL1                S3_0_C4_C6_0
#define ICC_DIR_EL1                S3_0_C12_C11_1
#define ICC_SGI1R_EL1              S3_0_C12_C11_5
#define ICC_EOIR1_EL1              S3_0_C12_C12_1
#define ICC_IAR1_EL1               S3_0_C12_C12_0
#define ICC_BPR1_EL1               S3_0_C12_C12_3
#define ICC_CTLR_EL1               S3_0_C12_C12_4
#define ICC_SRE_EL1                S3_0_C12_C12_5
#define ICC_IGRPEN1_EL1            S3_0_C12_C12_7

#define ICC_SGIxR_EL1_INTID_SHIFT  24

namespace gic {

class gic_v3_dist : public gic_dist {
public:
    gic_v3_dist(mmu::phys b, size_t l) : gic_dist(b, l) {}

    void enable();
    void disable();

    void wait_for_write_complete();
};

//Redistributor interface
class gic_v3_redist {
public:
    gic_v3_redist(mmu::phys b, size_t l);

    void init_cpu_base(int smp_idx);
    void init_lpis(int smp_idx, u64 prop_base, u64 pend_base);

    u32 read_at_offset(int smp_idx, u32 offset);
    u64 read64_at_offset(int smp_idx, u32 offset);
    void write_at_offset(int smp_idx, u32 offset, u32 value);
    void write64_at_offset(int smp_idx, u32 offset, u64 value);

    void init_rdbase(int smp_idx, bool pta);
    inline mmu::phys rdbase(int smp_idx) { return _rdbases[smp_idx]; }

    void wait_for_write_complete();
private:
    mmu::phys _base;
    mmu::phys *_cpu_bases;
    mmu::phys *_rdbases;
};

//See https://developer.arm.com/documentation/ddi0601/2024-09/External-Registers/GITS-BASER-n---ITS-Table-Descriptors
#define GITS_PAGE_SIZE(baser)      (((baser) & 0x300) >> 8) //Capture bits [9:8]
#define GITS_ITT_entry_size(type)  (((type) & 0xf0) >> 4)

#define ITS_MAPC_V                 (1ull << 63)
#define ITS_MAPD_V                 (1ull << 63)

#define GITS_TABLE_TYPE(baser)     (((baser) & 0x700000000000000ull) >> 56) //Capture bits [58:56]
#define GITS_TABLE_DEVICES_TYPE     0b001
#define GITS_TABLE_COLLECTIONS_TYPE 0b100

#define GITS_TABLE_PAGE_SIZE_4K     0b00
#define GITS_TABLE_PAGE_SIZE_16K    0b01
#define GITS_TABLE_PAGE_SIZE_64K    0b10

#define GITS_TABLE_BASE_PA_MASK     0xfffffffff000
#define GITS_BASER_VALID            0x8000000000000000ull

#define GITS_TABLE_NUM_MAX          8

#define GITS_CBASER_VALID           0x8000000000000000ull

#define GITS_CTLR_ENABLED           0x1
#define GITS_TYPER_PTA              0x80000 //Bit 19 - see https://developer.arm.com/documentation/ddi0601/2024-09/External-Registers/GITS-TYPER--ITS-Type-Register

enum class gic_its_reg : unsigned int {
    GICITS_CTLR    = 0x0000, /* Reg */
    GICITS_TYPER   = 0x0008, /* Reg */
    GICITS_CBASER  = 0x0080, /* Reg */
    GICITS_CWRITER = 0x0088, /* Reg */
    GICITS_CREADR  = 0x0090, /* Reg */
    GICITS_BASER   = 0x0100, /* The base address and size of the ITS tables reg*/
};

enum class gic_its_cmd : unsigned int {
    ITS_CMD_CLEAR   = 0x04,
    ITS_CMD_DISCARD = 0x0f,
    ITS_CMD_INT     = 0x03,
    ITS_CMD_INV     = 0x0c,
    ITS_CMD_INVALL  = 0x0d,
    ITS_CMD_MAPC    = 0x09,
    ITS_CMD_MAPD    = 0x08,
    ITS_CMD_MAPI    = 0x0b,
    ITS_CMD_MAPTI   = 0x0a,
    ITS_CMD_MOVALL  = 0x0e,
    ITS_CMD_MOVI    = 0x01,
    ITS_CMD_SYNC    = 0x05,
};

struct its_cmd {
    u64 data[4];
};

//
//Interrupt Translation Service interface
class gic_v3_its {
public:
    gic_v3_its(mmu::phys b, size_t l);

    u64 read_reg64(gic_its_reg r);
    u64 read_reg64_at_offset(gic_its_reg r, u32 offset);
    void write_reg(gic_its_reg r, u32 v);
    void write_reg64(gic_its_reg r, u64 v);
    void write_reg64_at_offset(gic_its_reg r, u32 offset, u64 v);

    void read_type_register();
    void initialize_cmd_queue();
    void enqueue_cmd(its_cmd *cmd);

    void cmd_mapd(u32 dev_id, u64 itt_pa, u64 itt_size);
    void cmd_mapti(u32 dev_id, int vector, int smp_idx);
    void cmd_movi(u32 dev_id, int vector, int smp_idx);
    void cmd_inv(u32 dev_id, int vector);
    void cmd_discard(u32 dev_id, int vector);
    void cmd_sync(mmu::phys rdbase);
    void cmd_mapc(int smp_idx, mmu::phys rdbase);

    bool is_typer_pta() { return _typer & GITS_TYPER_PTA; }
    u64 itt_entry_size() { return GITS_ITT_entry_size(_typer); }

    mmu::phys base() { return _base; }

private:
    mmu::phys _base;
    void *_cmd_queue;
    u64 _typer;
};

constexpr int max_sgi_cpus = 16;

class gic_v3_driver : public gic_driver {
public:
    gic_v3_driver(mmu::phys d, size_t d_len,
                  mmu::phys r, size_t r_len,
                  mmu::phys i, size_t i_len) :
        _gicd(d, d_len), _gicrd(r, r_len), _gits(i, i_len) {}

    virtual void init_on_primary_cpu()
    {
        _gicrd.init_cpu_base(0);
        init_lpis(0);
        init_dist();
        init_redist(0);
        init_its(0);
    }

    virtual void init_on_secondary_cpu(int smp_idx)
    {
        _gicrd.init_cpu_base(smp_idx);
        init_lpis(smp_idx);
        init_redist(smp_idx);
        init_its(smp_idx);
    }

    virtual void mask_irq(unsigned int id);
    virtual void unmask_irq(unsigned int id);

    virtual void set_irq_type(unsigned int id, irq_type type);

    virtual void send_sgi(sgi_filter filter, int smp_idx, unsigned int vector);

    virtual unsigned int ack_irq();
    virtual void end_irq(unsigned int iar);

    virtual void allocate_msi_dev_mapping(pci::function* dev);

    virtual void initialize_msi_vector(unsigned int vector) {}
    virtual void map_msi_vector(unsigned int vector, pci::function* dev, u32 target_cpu);
    virtual void unmap_msi_vector(unsigned int vector, pci::function* dev);
    virtual void msi_format(u64 *address, u32 *data, int vector);

private:
    void init_lpis(int smp_idx);
    void init_dist();
    void init_redist(int smp_idx);
    void init_its_device_or_collection_table(int idx);
    void init_its(int smp_idx);

    u32 pci_device_id(pci::function* dev);

    gic_v3_dist _gicd;
    gic_v3_redist _gicrd;
    gic_v3_its _gits;
    u64 _mpids_by_smpid[max_sgi_cpus];

    u8 *_lpi_config_table;
    u64 _lpi_prop_base;
    u64 *_lpi_pend_bases;

    u16 _msi_vector_num;

    std::unordered_map<u32, void*> _itt_by_device_id;
    std::unordered_map<unsigned int, u32> _cpu_by_vector;
    irq_spinlock_t _smp_init_its_lock;
};

}

#endif
