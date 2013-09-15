/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/pci.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-bridge.hh"

namespace pci {

    bridge::bridge(u8 bus, u8 device, u8 func)
        : function(bus, device, func)
    {

    }

    bridge::~bridge()
    {

    }

    bool bridge::parse_pci_config(void)
    {
        function::parse_pci_config();
        return (true);
    }

}
