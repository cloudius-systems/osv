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

#define GICD_CTLR_WRITE_COMPLETE   (1UL << 31)
#define GICD_CTLR_ARE_NS           (1U << 4)
#define GICD_CTLR_ENABLE_G1NS      (1U << 1)
#define GICD_CTLR_ENABLE_G0        (1U << 0)

#define GICD_ICFGR_MASK            0x3
#define GICD_ICFGR_TRIG_LVL        (0 << 1)
#define GICD_ICFGR_TRIG_EDGE       (1 << 1)
#define GICD_ICFGR_TRIG_MASK       0x2

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

#define GICR_STRIDE                (0x20000)
#define GICR_SGI_BASE              (0x10000)

#define GICR_CTLR                  (0x0000)
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

#define GICR_WAKER_ProcessorSleep  (1U << 1)
#define GICR_WAKER_ChildrenAsleep  (1U << 2)

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

#define GICD_DEF_PPI_ICENABLERn	   0xffff0000
#define GICD_DEF_SGI_ISENABLERn    0xffff

#define GICC_CTLR_EL1_EOImode_drop (1U << 1)

#define GICR_I_PER_ICENABLERn      32
#define GICR_I_PER_ISENABLERn      32

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
    gic_v3_dist(mmu::phys b) : gic_dist(b) {}

    void enable();
    void disable();

    void wait_for_write_complete();
};

//Redistributor interface
class gic_v3_redist {
public:
    gic_v3_redist(mmu::phys b) : _base(b) {}

    u32 read_at_offset(int smp_idx, u32 offset);
    void write_at_offset(int smp_idx, u32 offset, u32 value);

    void wait_for_write_complete();
private:
    mmu::phys _base;
};

constexpr int max_sgi_cpus = 16;

class gic_v3_driver : public gic_driver {
public:
    gic_v3_driver(mmu::phys d, mmu::phys r) : _gicd(d), _gicr(r) {}

    virtual void init_on_primary_cpu()
    {
        init_dist();
        init_redist(0);
    }

    virtual void init_on_secondary_cpu(int smp_idx) { init_redist(smp_idx); }

    virtual void mask_irq(unsigned int id);
    virtual void unmask_irq(unsigned int id);

    virtual void set_irq_type(unsigned int id, irq_type type);

    virtual void send_sgi(sgi_filter filter, int smp_idx, unsigned int vector);

    virtual unsigned int ack_irq();
    virtual void end_irq(unsigned int iar);
private:
    void init_dist();
    void init_redist(int smp_idx);

    gic_v3_dist _gicd;
    gic_v3_redist _gicr;
    u64 _mpids_by_smpid[max_sgi_cpus];
};

}

#endif
