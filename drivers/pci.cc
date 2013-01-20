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

#include "drivers/pci.hh"
#include "debug.hh"
#include "drivers/device-factory.hh"
#include "drivers/driver.hh"

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

u16 read_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset)
{
    prepare_pci_config_access(bus, slot, func, offset);
    return inw(PCI_CONFIG_DATA);
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

void write_pci_config_word(u8 bus, u8 slot, u8 func, u8 offset, u16 val)
{
    prepare_pci_config_access(bus, slot, func, offset);
    outw(val, PCI_CONFIG_DATA);
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
	    // adds a new line every 16 bytes
		debug(fmt("%02x ") % (unsigned int)val, (i % 16 == 15));
	}
}

void pci_devices_print(void)
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

void pci_device_enumeration(void)
{
    u16 bus, slot, func;
    DeviceFactory* factory = DeviceFactory::Instance();


    for (bus = 0; bus < 256; bus++)
        for (slot = 0; slot < 32; slot++)
            for (func = 0; func < 8; func++) {

                if (read_pci_config(bus, slot, func, PCI_CLASS_REVISION) == 0xffffffff)
                    continue;

                    factory->AddDevice(bus, slot, func);

                    // test for multiple functions
                    if (func == 0 &&
                        !(read_pci_config_byte(bus, slot, func, PCI_HEADER_TYPE) & PCI_HEADER_MULTI_FUNC))
                            break;
            }
}

Bar::Bar(int n, Driver* d) {
    u32 bar = read_pci_config(d->getBus(), d->getSlot(), d->getFunc(), PCI_BAR0_ADDR + n*4);
    _type = (bar & BAR_TYPE)? BAR_MMIO:BAR_IO;

    _addr = (_type == BAR_MMIO)? bar & 0xfffffff0 : bar & 0xfffffffc;
    debug(fmt("Device vid %x has io bar %d: addr=%x") % d->getSubsysVid() % n % _addr);
}


}
