/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mmio.hh>
#include <osv/mmu.hh>

#include "gic-common.hh"

namespace gic {

gic_dist::gic_dist(mmu::phys b, size_t l) : _base(b)
{
    mmu::linear_map((void *)_base, _base, l, "gic_dist", mmu::page_size, mmu::mattr::dev);
}

u32 gic_dist::read_reg(gicd_reg reg)
{
    return mmio_getl((mmioaddr_t)_base + (u32)reg);
}

void gic_dist::write_reg(gicd_reg reg, u32 value)
{
    mmio_setl((mmioaddr_t)_base + (u32)reg, value);
}

u32 gic_dist::read_reg_at_offset(u32 reg, u32 offset)
{
    return mmio_getl((mmioaddr_t)_base + reg + offset);
}

void gic_dist::write_reg_at_offset(u32 reg, u32 offset, u32 value)
{
    mmio_setl((mmioaddr_t)_base + reg + offset, value);
}

void gic_dist::write_reg64_at_offset(u32 reg, u32 offset, u64 value)
{
    mmio_setq((mmioaddr_t)_base + reg + offset, value);
}

unsigned int gic_dist::read_number_of_interrupts()
{
    return ((read_reg(gicd_reg::GICD_TYPER) & 0x1f) + 1) * 32;
}

class gic_driver *gic;
}
