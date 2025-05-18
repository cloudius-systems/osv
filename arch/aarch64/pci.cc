/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/align.hh>

#include <osv/pci.hh>
#include <osv/kernel_config_logger_debug.h>
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
static u64 pci_io_next = 0x1000;
static u64 pci_mem_next = 0;

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
    pci_mem_next = addr;
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
    debug("PCI irqmap\n");
    for (auto it = pci_irqmap.begin(); it != pci_irqmap.end(); ++it) {
        debugf("Bus,Device,Function 0x%08x -> SPI irq 0x%04x\n",
              (*it).first, (*it).second);
        debugf("B,D,F irqmap-mask   0x%08x\n", pci_irqmask);
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

u32 pci::function::arch_add_bar(u32 val, u32 pos, bool is_mmio, bool is_64, u64 addr_size)
{
    //Read pre-allocated address if any
    u64 addr_64 = val & (is_mmio ? pci::function::PCI_BAR_MEM_ADDR_LO_MASK : pci::function::PCI_BAR_PIO_ADDR_MASK);
    if (is_64) {
        u32 addr_hi = pci_readl(pos + 4);
        addr_64 |= ((u64)addr_hi << 32);
    }

    //If address has not been alocated by firmware, then allocate it
    if (!addr_64) {
        assert(is_mmio ? pci_mem_next : pci_io_next);
        addr_64 = is_mmio ? pci_mem_next : pci_io_next;

        //According to the "Address and size of the BAR" in
        //https://wiki.osdev.org/PCI#Base_Address_Registers
        //which reads this: "The BAR register is naturally aligned and
        //as such you can only modify the bits that are set. For example,
        //if a device utilizes 16 MB it will have BAR0 filled with 0xFF000000
        //(0x1000000 after decoding) and you can only modify the upper 8-bits."
        //it can be implied that the bar has to be aligned to the maximum
        //of its size and 16
        size_t bar_size = std::max((size_t)16, (size_t)addr_size);
        addr_64 = align_up(addr_64, bar_size);
        if (is_mmio) {
            pci_mem_next = addr_64 + bar_size;
        } else {
            pci_io_next = addr_64 + bar_size;
        }

        val |= (u32)addr_64;
        pci_writel(pos, val);

        if (is_64) {
            pci_writel(pos + 4, addr_64 >> 32);
        }
    }

    pci_d("arch_add_bar(): pos:%d, val:%x, mmio:%d, addr_64:%lx, addr_size:%lx",
        pos, val, is_mmio, addr_64, addr_size);
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
    /* add the SPI base number 32 to the irq id */
    irq_id += 32;
#if CONF_logger_debug
    debugf("get_pci_irq_line: bdfp  = %u, irqid = %d\n", bdfp, irq_id);
#endif
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
    auto address = build_config_address(bus, slot, func, offset);
    if (address < pci_cfg_len) {
        data = (u32 *)(pci_cfg_base + address);
        return *data;
    } else {
        return 0xffffffff;
    }
}

u16 read_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset)
{
    volatile u16 *data;
    auto address = build_config_address(bus, slot, func, offset);
    if (address < pci_cfg_len) {
        data = (u16 *)(pci_cfg_base + address);
        return *data;
    } else {
        return 0xffff;
    }
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
    volatile u8 *data;
    auto address = build_config_address(bus, slot, func, offset);
    if (address < pci_cfg_len) {
        data = (u8 *)(pci_cfg_base + address);
        return *data;
    } else {
        return 0xff;
    }
}

void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val)
{
    volatile u32 *data;
    auto address = build_config_address(bus, slot, func, offset);
    if (address < pci_cfg_len) {
        data = (u32 *)(pci_cfg_base + address);
        *data = val;
    } else {
        abort("Trying to write beyond PCI config area");
    }
}

void write_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset, u16 val)
{
    volatile u16 *data;
    auto address = build_config_address(bus, slot, func, offset);
    if (address < pci_cfg_len) {
        data = (u16 *)(pci_cfg_base + address);
        *data = val;
    } else {
        abort("Trying to write beyond PCI config area");
    }
}

void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val)
{
    volatile u8 *data;
    auto address = build_config_address(bus, slot, func, offset);
    if (address < pci_cfg_len) {
        data = (u8 *)(pci_cfg_base + address);
        *data = val;
    } else {
        abort("Trying to write beyond PCI config area");
    }
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
