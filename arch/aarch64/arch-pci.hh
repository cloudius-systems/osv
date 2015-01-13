/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_PCI_HH
#define ARCH_PCI_HH

#include <osv/mmio.hh>
#include "exceptions.hh"
#include <osv/interrupt.hh>
#include "drivers/pci-device.hh"

namespace pci {

void set_pci_ecam(bool is_ecam);
bool get_pci_ecam();

void set_pci_cfg(u64 addr, size_t len);
u64 get_pci_cfg(size_t *len);
void set_pci_io(u64 addr, size_t len);
u64 get_pci_io(size_t *len);
void set_pci_mem(u64 addr, size_t len);
u64 get_pci_mem(size_t *len);

/* set the pci irqmap converting phys.hi addresses to gic irq ids */
void set_pci_irqmap(u32 *bfds, int *irq_ids, int count, u32 mask);

/* dump the pci irqmap, useful for debugging */
void dump_pci_irqmap();

/* gets the gic interrupt id for the passed pci device */
unsigned get_pci_irq_line(pci::device &dev);

void outb(u8 val, u16 port);
void outw(u16 val, u16 port);
void outl(u32 val, u16 port);
u8 inb(u16 port);
u16 inw(u16 port);
u32 inl(u16 port);

} /* namespace pci */

class pci_interrupt : public spi_interrupt {
public:
    pci_interrupt(pci::device &dev, std::function<bool ()> a,
                  std::function<void ()> h)
        : spi_interrupt(gic::irq_type::IRQ_TYPE_LEVEL,
                        pci::get_pci_irq_line(dev), a, h) {};
};

#endif /* ARCH_PCI_HH */
