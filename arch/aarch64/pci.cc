/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/align.hh>

#include <osv/pci.hh>
#include "drivers/pci-function.hh"
#include "arch-dtb.hh"

namespace pci {

static volatile char *pci_cfg_base;
static volatile char *pci_io_base;
static volatile char *pci_mem_base;

static size_t pci_cfg_len;
static size_t pci_io_len;
static size_t pci_mem_len;

static bool ecam;

/* TODO: find out whether this is documented somewhere.
 * We cannot use the first page of I/O space.
 * QEMU silently discards programming the BARs to address zero.
 * Linux seems to skip the whole first page on ARM, so we do the same.
 */
static u64 pci_io_off = 0x1000;
static u64 pci_mem_off = 0;

/* this maps PCI addresses as returned by build_config_address
 * to platform IRQ numbers. */
static std::multimap<u32, int> pci_irqmap;
static u32 pci_irqmask;

void set_pci_ecam(bool is_ecam)
{
    ecam = is_ecam;
}

bool get_pci_ecam()
{
    return ecam;
}

void set_pci_cfg(u64 addr, size_t len)
{
    pci_cfg_base = (char *)addr;
    pci_cfg_len = len;
}

u64 get_pci_cfg(size_t *len)
{
    *len = pci_cfg_len;
    return (u64)pci_cfg_base;
}

void set_pci_io(u64 addr, size_t len)
{
    pci_io_base = (char *)addr;
    pci_io_len = len;
}

u64 get_pci_io(size_t *len)
{
    *len = pci_io_len;
    return (u64)pci_io_base;
}

void set_pci_mem(u64 addr, size_t len)
{
    pci_mem_base = (char *)addr;
    pci_mem_len = len;
}

u64 get_pci_mem(size_t *len)
{
    *len = pci_mem_len;
    return (u64)pci_mem_base;
}

void set_pci_irqmap(u32 *bdfs, int *irq_ids, int count, u32 mask)
{
    pci_irqmask = mask;
    for (int i = 0; i < count; i++) {
        pci_irqmap.insert(std::make_pair(bdfs[i], irq_ids[i]));
    }
}

void dump_pci_irqmap()
{
    debug_ll("PCI irqmap\n");
    for (auto it = pci_irqmap.begin(); it != pci_irqmap.end(); ++it) {
        debug_ll("Bus,Device,Function 0x%08x -> SPI irq 0x%04x\n",
                 (*it).first, (*it).second);
        debug_ll("B,D,F irqmap-mask   0x%08x\n", pci_irqmask);
    }
}

static int get_pci_irq_from_bdfp(u32 bdfp)
{
    int irq_id = -1;
    bdfp &= pci_irqmask;

    auto rng = pci_irqmap.equal_range(bdfp);

    for (auto it = rng.first; it != rng.second; it++) {
        if (irq_id < 0) {
            irq_id = (*it).second;
        } else {
            /* we do not support multiple irqs per slot (yet?) */
            abort();
        }
    }
    return irq_id;
}

u32 pci::bar::arch_add_bar(u32 val)
{
    u64 *off = _is_mmio ? &pci_mem_off : &pci_io_off;
    u64 addr = _is_mmio ? (u64)pci_mem_base + pci_mem_off : *off;

    *off += _addr_size;
    *off = align_up(*off, (size_t)16);

    val &= _is_mmio ? ~pci::bar::PCI_BAR_MEM_ADDR_LO_MASK : ~pci::bar::PCI_BAR_PIO_ADDR_MASK;
    val |= align_down(addr, (size_t)16);

    _dev->pci_writel(_pos, val);

    if (_is_64) {
        _dev->pci_writel(_pos + 4, addr >> 32);
    }

    return val;
}

unsigned get_pci_irq_line(pci::device &dev)
{
    u32 bdfp;
    u8 b, d, f, p; /* BEWARE, bdf written by get_bdf using references */
    dev.get_bdf(b, d, f);  /* arguments written to (not good.) */

    p = dev.get_interrupt_pin();
    bdfp = b << DTB_PHYSHI_B_SH | d << DTB_PHYSHI_D_SH | f << DTB_PHYSHI_F_SH;
    bdfp |= p & DTB_PIN_MASK;

    int irq_id = pci::get_pci_irq_from_bdfp(bdfp);
    assert(irq_id > 0);
    debug_early_u64("get_pci_irq_line: bdfp  = ", (u64)bdfp);
    /* add the SPI base number 32 to the irq id */
    irq_id += 32;
    debug_early_u64("get_pci_irq_line: irqid = ", (u64)irq_id);
    return irq_id;
}

static inline volatile
u32 build_config_address(u8 bus, u8 slot, u8 func, u8 offset)
{
    u32 addr;
    if (ecam) {
        addr = bus << 20 | slot << 15 | func << 12 | offset;
    } else {
        addr = bus << 16 | slot << 11 | func << 8 | offset;
    }
    return addr;
}

u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
    volatile u32 *data;
    data = (u32 *)(pci_cfg_base + build_config_address(bus, slot, func, offset));
    return *data;
}

u16 read_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset)
{
    volatile u16 *data;
    data = (u16 *)(pci_cfg_base + build_config_address(bus, slot, func, offset));
    return *data;
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
    volatile u8 *data;
    data = (u8 *)(pci_cfg_base + build_config_address(bus, slot, func, offset));
    return *data;
}

void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val)
{
    volatile u32 *data;
    data = (u32 *)(pci_cfg_base + build_config_address(bus, slot, func, offset));
    *data = val;
}

void write_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset, u16 val)
{
    volatile u16 *data;
    data = (u16 *)(pci_cfg_base + build_config_address(bus, slot, func, offset));
    *data = val;
}

void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val)
{
    volatile u8 *data;
    data = (u8 *)(pci_cfg_base + build_config_address(bus, slot, func, offset));
    *data = val;
}

void outb(u8 val, u16 port)
{
    u64 addr = (u64)pci_io_base + port;
    mmio_setb((mmioaddr_t)addr, val);
}
void outw(u16 val, u16 port)
{
    u64 addr = (u64)pci_io_base + port;
    mmio_setw((mmioaddr_t)addr, val);
}
void outl(u32 val, u16 port)
{
    u64 addr = (u64)pci_io_base + port;
    mmio_setl((mmioaddr_t)addr, val);
}

u8 inb(u16 port)
{
    u64 addr = (u64)pci_io_base + port;
    return mmio_getb((mmioaddr_t)addr);
}
u16 inw(u16 port)
{
    u64 addr = (u64)pci_io_base + port;
    return mmio_getw((mmioaddr_t)addr);
}
u32 inl(u16 port)
{
    u64 addr = (u64)pci_io_base + port;
    return mmio_getl((mmioaddr_t)addr);
}

} /* namespace pci */
