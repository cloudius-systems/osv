/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *               2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#define CONF_drivers_xen 1
#include "xen.hh"
#include "xenplatform-pci.hh"

#define XEN_VENDOR_ID 0x5853
#define XEN_DEVICE_ID 0x0001

namespace xenfront {

hw_driver* xenplatform_pci::probe(hw_device *dev)
{
    if (!processor::features().xen_pci)
        return nullptr;

    if (dev->get_id() == hw_device_id(XEN_VENDOR_ID, XEN_DEVICE_ID)) {
	auto pci_dev = dynamic_cast<pci::device*>(dev);
        return new xenplatform_pci(*pci_dev);
    }

    return nullptr;
}

xenplatform_pci::xenplatform_pci(pci::device& dev)
    : hw_driver()
    , _dev(dev)
{
    _dev.set_bus_master(true);
    if (!processor::features().xen_vector_callback) {
        int irqno = _dev.get_interrupt_line();
        _pgsi.reset(xen::xen_set_callback(irqno));
    }
    _xenbus.reset(xenfront::xenbus::probe(NULL));
}
}
