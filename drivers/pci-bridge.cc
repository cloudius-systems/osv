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
