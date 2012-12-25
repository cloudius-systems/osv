#ifndef ARCH_X86_PCI_H
#define ARCH_X86_PCI_H

#include <stdint.h>
#include "arch/x64/processor.hh"

namespace pci {

	typedef unsigned long ulong;
	typedef uint8_t u8;
	typedef uint16_t u16;
	typedef uint32_t u32;
	typedef uint64_t u64;

	inline u8 inb(u16 port) {
	    return processor::x86::inb(port);
	}

	inline u16 inw(u16 port) {
	    return processor::x86::inw(port);
	}

	inline u32 inl(u16 port) {
	    return processor::x86::inl(port);
	}

	inline void outb(u8 val, u16 port) {
		processor::x86::outb(val, port);

    }

	inline void outw(u16 val, u16 port) {
		processor::x86::outw(val, port);

    }

	inline void outl(u32 val, u16 port) {
		processor::x86::outl(val, port);

    }
};

#endif
