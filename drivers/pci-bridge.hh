#ifndef PCI_BRIDGE_H
#define PCI_BRIDGE_H

#include "drivers/pci.hh"
#include "drivers/pci-function.hh"

namespace pci {

    class pci_bridge: public pci_function {
    public:
        pci_bridge(u8 bus, u8 device, u8 func);
        virtual ~pci_bridge();

        virtual bool parse_pci_config(void);

    protected:
    };

}

#endif // PCI_BRIDGE_H
