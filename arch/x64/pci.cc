#include "arch/x64/pci.hh"
#include "debug.hh"

namespace pci {

static inline void prepare_pci_config_access(u8 bus, u8 slot, u8 func, u8 offset)
{
    outl(PCI_CONFIG_ADDRESS_ENABLE | (bus<<PCI_BUS_OFFSET) | (slot<<PCI_SLOT_OFFSET) | (func<<PCI_FUNC_OFFSET) | offset, PCI_CONFIG_ADDRESS);
}

u32 read_pci_config(u8 bus, u8 slot, u8 func, u8 offset)
{
    prepare_pci_config_access(bus, slot, func, offset);
	return inl(PCI_CONFIG_DATA);
}

u8 read_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset)
{
	prepare_pci_config_access(bus, slot, func, offset);
	return inb(PCI_CONFIG_DATA + (offset&3));
}

void write_pci_config(u8 bus, u8 slot, u8 func, u8 offset, u32 val)
{
    prepare_pci_config_access(bus, slot, func, offset);
	outl(val, PCI_CONFIG_DATA);
}

void write_pci_config_byte(u8 bus, u8 slot, u8 func, u8 offset, u8 val)
{
    prepare_pci_config_access(bus, slot, func, offset);
	outb(val, PCI_CONFIG_DATA + (offset&3));
}

void pci_device_print(u8 bus, u8 slot, u8 func)
{
	int i;
	u8 val;

	debug(fmt("Config space of: %02x:%02x:%02x") % (unsigned int)bus % (unsigned int)slot % (unsigned int)func);

	for (i = 0; i < 256; i ++) {
	    val = read_pci_config_byte(bus, slot, func, i);
		debug(fmt("%02x ") % (unsigned int)val, (i % 16 == 15));
	}
}

void pci_devices_print(void)
{
	unsigned bus, slot, func;

	for (bus = 0; bus < 256; bus++) {
		for (slot = 0; slot < 32; slot++) {
			for (func = 0; func < 8; func++) {
				u32 revision;
				u8 header_type;

				if ((revision = read_pci_config(bus, slot, func, PCI_CLASS_REVISION)) == 0xffffffff)
					continue;

				pci_device_print(bus, slot, func);

				// test for multiple functions
				if (func == 0) {
					header_type = read_pci_config_byte(bus, slot, func, PCI_HEADER_TYPE);
					if (!(header_type & PCI_HEADER_MULTI_FUNC))
						break;
				}
			}
		}
	}
}

}
