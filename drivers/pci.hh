/* This header is BSD licensed so anyone can use the definitions to implement
 * compatible drivers/servers.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of IBM nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL IBM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE. */

#ifndef ARCH_X86_PCI_H
#define ARCH_X86_PCI_H

#include <stdint.h>
#include "pci-function.hh"
#include "processor.hh"
#include <osv/types.h>

class Driver;

#define pci_tag "pci"
#define pci_d(fmt)   logger::instance()->wrt(pci_tag, logger_debug, (fmt))
#define pci_i(fmt)   logger::instance()->wrt(pci_tag, logger_info, (fmt))
#define pci_w(fmt)   logger::instance()->wrt(pci_tag, logger_warn, (fmt))
#define pci_e(fmt)   logger::instance()->wrt(pci_tag, logger_error, (fmt))

namespace pci {

using processor::inb;
using processor::inw;
using processor::inl;
using processor::outb;
using processor::outw;
using processor::outl;

    enum pc_early_defines {
        PCI_CONFIG_ADDRESS = 0xcf8,
        PCI_CONFIG_DATA    = 0xcfc,
        PCI_BUS_OFFSET     = 16,
        PCI_SLOT_OFFSET    = 11,
        PCI_FUNC_OFFSET    = 8,
        PCI_CONFIG_ADDRESS_ENABLE = 0x80000000,
        PCI_COMMAND_OFFSET = 0x4,
        PCI_BUS_MASTER_BIT = 0x2,
        PCI_STATUS_OFFSET  = 0x6,
        PCI_CLASS_REVISION = 0x8,
        PCI_CLASS_OFFSET   = 0xb,
        PCI_SUBCLASS_OFFSET= 0xa,
        PCI_HEADER_TYPE    = 0xe,
        PCI_SUBSYSTEM_ID   = 0x2e,
        PCI_SUBSYSTEM_VID  = 0x2c,
        PCI_HEADER_MULTI_FUNC = 0x80,
        PCI_BAR0_ADDR      = 0x10,
        PCI_CAPABILITIES_PTR = 0x34,
    };

    u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset);
    u16 read_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset);
    u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset);
    void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val);
    void write_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset, u16 val);
    void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val);
    void pci_device_print(u8 bus, u8 slot, u8 func);
    void pci_devices_print(void);
    void pci_device_enumeration(void);

};

#endif
