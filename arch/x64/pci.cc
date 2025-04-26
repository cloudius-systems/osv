/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/pci.hh>
#include "drivers/pci-function.hh"

namespace pci {

    /* 31     30  -  24  23 - 16  15 - 11  10 - 8     7 - 2    1 - 0
     * Enable Reserved   Bus Nr   Device   Function  Register  0   0
     *
     * least significant byte selects the offset into the 256-byte cfg space.
     * Since all reads and writes must be both 32-bits and aligned, the two
     * lowest bits must always be zero.
     * For reading and writing 8-bit and 16-bit quantities, we have to
     * perform the unaligned access in software by aligning the address,
     * followed by masking / shifting the answer.
     * In practice nobody seems to be following this (neither Linux nor OSv)
     * because it is an incredible pain to WRITE (reading would be fine).
     * So the code actually assumes 8-bit and 16-bit aligned access is also
     * possible.
     */
static inline u32 build_config_address(u8 bus, u8 slot, u8 func, u8 offset) {
    return bus << PCI_BUS_OFFSET | slot << PCI_SLOT_OFFSET |
        func << PCI_FUNC_OFFSET | (offset & ~0x03);
}

static inline void prepare_pci_config_access(u8 bus, u8 slot, u8 func, u8 offset)
{
    u32 address = build_config_address(bus, slot, func, offset);
    outl(PCI_CONFIG_ADDRESS_ENABLE | address, PCI_CONFIG_ADDRESS);
}

u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
    prepare_pci_config_access(bus, slot, func, offset);
    return inl(PCI_CONFIG_DATA);
}

u16 read_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset)
{
    prepare_pci_config_access(bus, slot, func, offset);
    return inw(PCI_CONFIG_DATA + (offset & 0x02));
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
    prepare_pci_config_access(bus, slot, func, offset);
    return inb(PCI_CONFIG_DATA + (offset & 0x03));
}

void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val)
{
    prepare_pci_config_access(bus, slot, func, offset);
    outl(val, PCI_CONFIG_DATA);
}

void write_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset, u16 val)
{
    prepare_pci_config_access(bus, slot, func, offset);
    outw(val, PCI_CONFIG_DATA + (offset & 0x02));
}

void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val)
{
    prepare_pci_config_access(bus, slot, func, offset);
    outb(val, PCI_CONFIG_DATA + (offset & 0x03));
}

u32 pci::function::arch_add_bar(u32 val, u32 pos, bool is_mmio, bool is_64, u64 addr_size)
{
    /* nothing to do on X86 since firmware already sets to sane values.
     * why is this not available on other archs (ARM) as well? Damn.
     */
    return val;
}

} /* namespace pci */
