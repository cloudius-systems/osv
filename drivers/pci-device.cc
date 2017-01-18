/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/pci.hh>
#include "drivers/pci-device.hh"
#include "drivers/pci-function.hh"

namespace pci {

    device::device(u8 bus, u8 device, u8 func)
        : function(bus, device, func),
          _subsystem_vid(0), _subsystem_id(0)
    {

    }

    device::~device()
    {

    }

    bool device::parse_pci_config()
    {
        function::parse_pci_config();

        // Read subsystem vendor id & id
        _subsystem_vid = pci_readw(PCI_CFG_SUBSYSTEM_VENDOR_ID);
        _subsystem_id = pci_readw(PCI_CFG_SUBSYSTEM_ID);

        // Parse PCI device BARs
        u32 pos = PCI_CFG_BAR_1;
        int idx = 1;

        function::set_bars_enable(true, true);

        while (pos <= PCI_CFG_BAR_6) {
            u32 bar_v = pci_readl(pos);

            if (bar_v == 0) {
                pos += 4;
                idx++;
                continue;
            }

            bar * pbar = new bar(this, pos);
            add_bar(idx++, pbar);

            pos += pbar->is_64() ? idx++, 8 : 4;
        }

        return true;
    }

    u16 device::get_subsystem_id()
    {
        return _subsystem_id;
    }

    u16 device::get_subsystem_vid()
    {
        return _subsystem_vid;
    }

    void device::dump_config()
    {
        function::dump_config();

        pci_d("    subsys_vid:subsys_id %x:%x", _subsystem_vid, _subsystem_id);
    }

}
