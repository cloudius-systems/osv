/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PCI_DEVICE_H
#define PCI_DEVICE_H

#include <osv/types.h>
#include "pci.hh"
#include "pci-function.hh"

namespace pci {

    class device: public function {
    public:
        device(u8 bus, u8 device, u8 func);
        virtual ~device();

        // Parse configuration space of pci_device
        virtual bool parse_pci_config(void);

        u16 get_subsystem_id(void);
        u16 get_subsystem_vid(void);

        virtual void dump_config(void);

    protected:

        u16 _subsystem_vid;
        u16 _subsystem_id;

    };


}

#endif
