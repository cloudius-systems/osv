#include "drivers/pci.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-bridge.hh"

namespace pci {

    pci_bridge::pci_bridge(u8 bus, u8 device, u8 func)
        : pci_function(bus, device, func)
    {

    }

    pci_bridge::~pci_bridge()
    {

    }

    bool pci_bridge::parse_pci_config(void)
    {
        pci_function::parse_pci_config();
        return (true);
    }

}
