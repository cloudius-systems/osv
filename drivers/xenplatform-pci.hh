/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *               2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef XENPLATFORM_PCI_H
#define XENPLATFORM_PCI_H

#include <drivers/driver.hh>
#include <drivers/xenfront-xenbus.hh>

namespace xenfront {

class xenplatform_pci : public hw_driver {

public:
    static hw_driver* probe(hw_device* dev);

    explicit xenplatform_pci(pci::device& dev);
    virtual void dump_config() { _dev.dump_config(); }
    virtual std::string get_name() const {
        return std::string("xen-platform-pci");
    }

private:
    pci::device& _dev;
    std::unique_ptr<gsi_level_interrupt> _pgsi;
    std::unique_ptr<hw_driver> _xenbus;
};
}
#endif /* XENPLATFORM_PCI_H */
