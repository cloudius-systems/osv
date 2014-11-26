/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PCI_CONFIG_HH
#define PCI_CONFIG_HH

namespace pci {
    /* each arch needs to implement these interfaces in its arch pci.cc module */
    u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset);
    u16 read_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset);
    u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset);
    void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val);
    void write_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset, u16 val);
    void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val);
} /* namespace pci */

#endif /* PCI_CONFIG_HH */
