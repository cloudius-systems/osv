/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include "mmu.hh"
#include "mmio.hh"

using namespace mmu;

void mmio_setb(mmioaddr_t addr, u8 val)
{
    *reinterpret_cast<volatile u8*>(addr) = val;
}

void mmio_setw(mmioaddr_t addr, u16 val)
{
    *reinterpret_cast<volatile u16*>(addr) = val;
}

void mmio_setl(mmioaddr_t addr, u32 val)
{
    *reinterpret_cast<volatile u32*>(addr) = val;
}

void mmio_setq(mmioaddr_t addr, u64 val)
{
    *reinterpret_cast<volatile u64*>(addr) = val;
}

u8 mmio_getb(mmioaddr_t addr)
{
    return (*reinterpret_cast<volatile u8*>(addr));
}

u16 mmio_getw(mmioaddr_t addr)
{
    return (*reinterpret_cast<volatile u16*>(addr));
}

u32 mmio_getl(mmioaddr_t addr)
{
    return (*reinterpret_cast<volatile u32*>(addr));
}

u64 mmio_getq(mmioaddr_t addr)
{
    return (*reinterpret_cast<volatile u64*>(addr));
}

mmioaddr_t mmio_map(u64 paddr, size_t size_bytes)
{
    char* map_to = mmu::phys_mem + paddr;
    linear_map(map_to, paddr, size_bytes, 4096);
    return map_to;
}

void mmio_unmap(mmioaddr_t addr, size_t size_bytes)
{
    // FIXME: implement
}
