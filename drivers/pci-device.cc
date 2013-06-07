#include "drivers/pci.hh"
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

    bool device::parse_pci_config(void)
    {
        function::parse_pci_config();

        // Read subsystem vendor id & id
        _subsystem_vid = pci_readw(PCI_CFG_SUBSYSTEM_VENDOR_ID);
        _subsystem_id = pci_readw(PCI_CFG_SUBSYSTEM_ID);

        // Parse PCI device BARs
        u32 pos = PCI_CFG_BAR_1;
        int idx = 1;

        while (pos <= PCI_CFG_BAR_6) {
            u32 bar_v = pci_readl(pos);

            if (bar_v == 0) {
                break;
            }

            bar * pbar = new bar(this, pos);
            add_bar(idx++, pbar);

            pos += pbar->is_64() ? 8 : 4;
        }

        return (true);
    }

    u16 device::get_subsystem_id(void)
    {
        return (_subsystem_id);
    }

    u16 device::get_subsystem_vid(void)
    {
        return (_subsystem_vid);
    }

    void device::dump_config(void)
    {
        function::dump_config();

        pci_d("    subsys_vid:subsys_id %x:%x\n", _subsystem_vid, _subsystem_id);
    }

}
