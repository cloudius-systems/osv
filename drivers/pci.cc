/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sstream>
#include <iomanip>

#include <osv/debug.hh>

#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include "drivers/device.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-bridge.hh"
#include "drivers/pci-device.hh"

namespace pci {

static inline void prepare_pci_config_access(u8 bus, u8 slot, u8 func, u8 offset)
{
    outl(PCI_CONFIG_ADDRESS_ENABLE | (bus<<PCI_BUS_OFFSET) | (slot<<PCI_SLOT_OFFSET) | (func<<PCI_FUNC_OFFSET) | (offset & ~0x03), PCI_CONFIG_ADDRESS);
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

void pci_device_print(u8 bus, u8 slot, u8 func)
{
    pci_d("Config space of: %02x:%02x:%02x",
        (unsigned int)bus, (unsigned int)slot, (unsigned int)func);

    for (u8 i = 0; i < 16; i++) {
        std::string row;
        for (u8 j = 0; j < 16; j++) {
            u8 val = read_pci_config_byte(bus, slot, func, i*16+j);
            std::stringstream ss;
            ss << std::setfill('0') << std::setw(2) << std::hex << (int)val;
            row += ss.str();
            row += " ";
        }

        pci_d(row.c_str());
    }
}

void pci_devices_print()
{
    u16 bus, slot, func;

    for (bus = 0; bus < 256; bus++) {
        for (slot = 0; slot < 32; slot++) {
            for (func = 0; func < 8; func++) {
                if (read_pci_config(bus, slot, func, PCI_CLASS_REVISION) == 0xffffffff)
                    continue;

                pci_device_print(bus, slot, func);

                // test for multiple functions
                if (func == 0 &&
                    !(read_pci_config_byte(bus, slot, func, PCI_HEADER_TYPE) & PCI_HEADER_MULTI_FUNC))
                        break;
            }
        }
    }
}

bool check_bus(u16 bus)
{
    bool found = false;
    u16 slot, func;
    for (slot = 0; slot < 32; slot++) {
        if (read_pci_config_word(bus, slot, 0, PCI_VENDOR_ID) == 0xffff)
            continue;

        for (func = 0; func < 8; func++) {

            if (read_pci_config(bus, slot, func, PCI_CLASS_REVISION) == 0xffffffff) {
                continue;
            }

            found = true;

            function * dev = nullptr;
            if (function::is_bridge(bus, slot, func)) {
                dev = new bridge(bus, slot, func);
                u8 sec_bus = read_pci_config_byte(bus, slot, func, PCI_CONFIG_SECONDARY_BUS);
                check_bus(sec_bus);
            } else {
                dev = new device(bus, slot, func);
            }

            bool parse_ok = dev->parse_pci_config();
            if (!parse_ok) {
                pci_e("Error: couldn't parse device config space %x:%x.%x",
                        bus, slot, func);
                break;
            }

            if (!device_manager::instance()->register_device(dev)) {
                pci_e("Error: couldn't register device %x:%x.%x",
                        bus, slot, func);
                //TODO: Need to beautify it as multiple instances of the device may exist
                delete dev;
            }

            // test for multiple functions
            if (func == 0 &&
                    !(read_pci_config_byte(bus, slot, func, PCI_HEADER_TYPE) & PCI_HEADER_MULTI_FUNC))
                break;
        }
    }

    return found;
}

void pci_device_enumeration()
{
    for (u16 bus = 0; bus < 256; bus++) {
        if (check_bus(bus))
            break;
    }
}
}
