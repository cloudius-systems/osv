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
#include "processor.hh"
#include "types.hh"

class Driver;

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

	//  Capability Register Offsets
	enum pci_capabilities_offsets {
	    PCI_CAP_OFF_ID      = 0x0,
	    PCI_CAP_OFF_NEXT    = 0x1
	};

	enum pci_capabilities {
	    PCI_CAP_PM          = 0x01,    // PCI Power Management
	    PCI_CAP_AGP         = 0x02,    // AGP
	    PCI_CAP_VPD         = 0x03,    // Vital Product Data
	    PCI_CAP_SLOTID      = 0x04,    // Slot Identification
	    PCI_CAP_MSI         = 0x05,    // Message Signaled Interrupts
	    PCI_CAP_CHSWP       = 0x06,    // CompactPCI Hot Swap
	    PCI_CAP_PCIX        = 0x07,    // PCI-X
	    PCI_CAP_HT          = 0x08,    // HyperTransport
	    PCI_CAP_VENDOR      = 0x09,    // Vendor Unique
	    PCI_CAP_DEBUG       = 0x0a,    // Debug port
	    PCI_CAP_CRES        = 0x0b,    // CompactPCI central resource control
	    PCI_CAP_HOTPLUG     = 0x0c,    // PCI Hot-Plug
	    PCI_CAP_SUBVENDOR   = 0x0d,    // PCI-PCI bridge subvendor ID
	    PCI_CAP_AGP8X       = 0x0e,    // AGP 8x
	    PCI_CAP_SECDEV      = 0x0f,    // Secure Device
	    PCI_CAP_EXPRESS     = 0x10,    // PCI Express
	    PCI_CAP_MSIX        = 0x11,    // MSI-X
	    PCI_CAP_SATA        = 0x12,    // SATA
	    PCI_CAP_PCIAF       = 0x13     // PCI Advanced Features
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

	class Bar {
	public:
	    Bar(int n, Driver* d);
	    enum TYPE {
	      BAR_TYPE = 1,
	      BAR_IO = 0,
	      BAR_MMIO = 1,
	    };

	    u32 read(u32 offset)  {return inl(_addr+offset);}
	    u16 readw(u32 offset) {return inw(_addr+offset);}
	    u8  readb(u32 offset) {return inb(_addr+offset);}
	    void write(u32 offset, u32 val) {outl(val, _addr+offset);}
	    void write(u32 offset, u16 val) {outw(val, _addr+offset);}
	    void write(u32 offset, u8 val)  {outb(val, _addr+offset);}

	private:
	    u32 _addr;
	    TYPE _type;
	};


    //  MSI-X definitions
    enum msix_pci_conf {
        PCIR_MSIX_CTRL = 0x2,
        PCIM_MSIXCTRL_MSIX_ENABLE = 0x8000,
        PCIM_MSIXCTRL_FUNCTION_MASK = 0x4000,
        PCIM_MSIXCTRL_TABLE_SIZE = 0x07FF,
        PCIR_MSIX_TABLE = 0x4,
        PCIR_MSIX_PBA = 0x8,
        PCIM_MSIX_BIR_MASK = 0x7,
        PCIM_MSIX_BIR_BAR_10 = 0,
        PCIM_MSIX_BIR_BAR_14 = 1,
        PCIM_MSIX_BIR_BAR_18 = 2,
        PCIM_MSIX_BIR_BAR_1C = 3,
        PCIM_MSIX_BIR_BAR_20 = 4,
        PCIM_MSIX_BIR_BAR_24 = 5,
        PCIM_MSIX_VCTRL_MASK = 0x1
    };

    //  Interesting values for PCI MSI-X
    struct msix_vector {
        u64 mv_address;         // Contents of address register.
        u32 mv_data;            // Contents of data register.
        int     mv_irq;
    };

    struct msix_table_entry {
        u32   mte_vector; //  1-based index into msix_vectors array.
        u32   mte_handlers;
    };

    struct pcicfg_msix {
        u16 msix_ctrl;                          //  Message Control
        u16 msix_msgnum;                        //  Number of messages
        u8 msix_location;                       //  Offset of MSI-X capability registers.
        u8 msix_table_bar;                      //  BAR containing vector table.
        u8 msix_pba_bar;                        //  BAR containing PBA.
        u32 msix_table_offset;
        u32 msix_pba_offset;
        int msix_alloc;                         //  Number of allocated vectors.
        int msix_table_len;                     //  Length of virtual table.
        struct msix_table_entry* msix_table;    //  Virtual table.
        struct msix_vector* msix_vectors;       //  Array of allocated vectors.
        Bar* msix_table_res;                    //  Resource containing vector table.
        Bar* msix_pba_res;                      //  Resource containing PBA.
    };

};

#endif
