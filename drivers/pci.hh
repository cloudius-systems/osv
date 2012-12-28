#ifndef ARCH_X86_PCI_H
#define ARCH_X86_PCI_H

#include <stdint.h>
#include "processor.hh"

namespace pci {

	typedef unsigned long ulong;
	typedef uint8_t u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;

	inline u8 inb(u16 port) {
	    return processor::inb(port);
	}

	inline u16 inw(u16 port) {
	    return processor::inw(port);
	}

	inline u32 inl(u16 port) {
	    return processor::inl(port);
	}

	inline void outb(u8 val, u16 port) {
		processor::outb(val, port);

    }

	inline void outw(u16 val, u16 port) {
		processor::outw(val, port);

    }

	inline void outl(u32 val, u16 port) {
		processor::outl(val, port);

    }

	enum pc_early_defines {
	    PCI_CONFIG_ADDRESS = 0xcf8,
	    PCI_CONFIG_DATA    = 0xcfc,
	    PCI_BUS_OFFSET     = 16,
	    PCI_SLOT_OFFSET    = 11,
	    PCI_FUNC_OFFSET    = 8,
	    PCI_CONFIG_ADDRESS_ENABLE = 0x80000000,
	    PCI_CLASS_REVISION = 0x8,
	    PCI_HEADER_TYPE    = 0xe,
	    PCI_HEADER_MULTI_FUNC = 0x80,
	};


	u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset);
	u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset);
	void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val);
	void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val);
	void pci_device_print(u8 bus, u8 slot, u8 func);
	void pci_devices_print(void);

};

#endif
