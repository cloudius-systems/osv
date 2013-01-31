#include "debug.hh"
#include "mmio.hh"
#include "pci.hh"
#include "device.hh"
#include "pci-function.hh"

using namespace hw;

namespace pci {

    pci_bar::pci_bar(pci_function* dev, u8 pos)
        : _dev(dev), _pos(pos),
          _addr_lo(0), _addr_hi(0), _addr_64(0), _addr_size(0),
          _addr_mmio(mmio_nullptr),
          _is_mmio(false), _is_64(false), _is_prefetchable(false)
    {
        init();
    }

    pci_bar::~pci_bar()
    {

    }

    void pci_bar::init(void)
    {
        u32 val = _dev->pci_readl(_pos);

        _is_mmio = ((val & PCI_BAR_MEMORY_INDICATOR_MASK) == PCI_BAR_MMIO);
        if (_is_mmio) {
            _addr_lo = val & PCI_BAR_MEM_ADDR_LO_MASK;
            _is_64 = ((val & PCI_BAR_MEM_ADDR_SPACE_MASK)
                == PCI_BAR_64BIT_ADDRESS);
            _is_prefetchable = ((val & PCI_BAR_PREFETCHABLE_MASK)
                == PCI_BAR_PREFETCHABLE);

            if (_is_64) {
                _addr_hi = _dev->pci_readl(_pos + 4);
            }

        } else {
            _addr_lo = val & PCI_BAR_PIO_ADDR_MASK;
        }

        _addr_64 = ((u64)_addr_hi << 32) | (u64)(_addr_lo);

        // Determine the bar size
        test_bar_size();
    }

    void pci_bar::test_bar_size(void)
    {
        u32 lo_orig = _dev->pci_readl(_pos);

        // Size test
        _dev->pci_writel(_pos, 0xFFFFFFFF);
        u32 lo = _dev->pci_readl(_pos);
        // Restore
        _dev->pci_writel(_pos, lo_orig);

        if (is_pio()) {
            lo &= PCI_BAR_PIO_ADDR_MASK;
        } else {
            lo &= PCI_BAR_MEM_ADDR_LO_MASK;
        }

        u32 hi = 0xFFFFFFFF;

        if (is_64()) {
            u32 hi_orig = _dev->pci_readl(_pos+4);
            _dev->pci_writel(_pos+4, 0xFFFFFFFF);
            hi = _dev->pci_readl(_pos+4);
            // Restore
            _dev->pci_writel(_pos+4, hi_orig);
        }

        u64 bits = (u64)hi << 32 | lo;
        _addr_size = ~bits + 1;
    }

    void pci_bar::map(void)
    {
        if (_is_mmio) {
            _addr_mmio = mmio_map(get_addr64(), get_size());
        }
    }

    void pci_bar::unmap(void)
    {
        if ((_is_mmio) && (_addr_mmio != mmio_nullptr)) {
            mmio_unmap(_addr_mmio, get_size());
        }
    }

    mmioaddr_t pci_bar::get_mmio(void)
    {
        return (_addr_mmio);
    }

    pci_function::pci_function(u8 bus, u8 device, u8 func)
        : _bus(bus), _device(device), _func(func), _have_msix(false)
    {

    }

    pci_function::~pci_function()
    {
        for (auto it = _bars.begin(); it != _bars.end(); it++) {
            delete (it->second);
        }
    }

    hw_device_id pci_function::get_id(void)
    {
        return (hw_device_id(_vendor_id, _device_id));
    }

    void pci_function::print(void)
    {
        dump_config();
    }

    void pci_function::reset(void)
    {
        // TODO: implement
    }

    bool pci_function::parse_pci_config(void)
    {
        _device_id = pci_readw(PCI_CFG_DEVICE_ID);
        _vendor_id = pci_readw(PCI_CFG_VENDOR_ID);
        _revision_id = pci_readb(PCI_CFG_REVISION_ID);
        _header_type = pci_readb(PCI_CFG_HEADER_TYPE);
        _base_class_code = pci_readb(PCI_CFG_CLASS_CODE0);
        _sub_class_code = pci_readb(PCI_CFG_CLASS_CODE1);
        _lower_class_code = pci_readb(PCI_CFG_CLASS_CODE2);

        // Parse capabilities
        bool parse_ok = parse_pci_capabilities();

        return parse_ok;
    }

    bool pci_function::parse_pci_capabilities(void)
    {
        // Parse MSI-X
        u8 off = find_capability(PCI_CAP_MSIX);
        if (off != 0xFF) {
            bool msi_ok = parse_pci_msix(off);
            return (msi_ok);
        }

        return true;
    }

    bool pci_function::parse_pci_msix(u8 off)
    {
        // Used for parsing MSI-x
        u32 val = 0;

        // Location within the configuration space
        _msix.msix_location = off;
        _msix.msix_ctrl = pci_readw(off + PCIR_MSIX_CTRL);
        _msix.msix_msgnum = (_msix.msix_ctrl & PCIM_MSIXCTRL_TABLE_SIZE) + 1;
        val = pci_readl(off + PCIR_MSIX_TABLE);
        _msix.msix_table_bar = val & PCIM_MSIX_BIR_MASK;
        _msix.msix_table_offset = val & ~PCIM_MSIX_BIR_MASK;
        val = pci_readl(off + PCIR_MSIX_PBA);
        _msix.msix_pba_bar = val & PCIM_MSIX_BIR_MASK;
        _msix.msix_pba_offset = val & ~PCIM_MSIX_BIR_MASK;

        // We've found an MSI-x capability
        _have_msix = true;

        return true;
    }

    void pci_function::get_bdf(u8& bus, u8 &device, u8& func)
    {
        bus = _bus;
        device = _device;
        func = _func;
    }

    void pci_function::set_bdf(u8 bus, u8 device, u8 func)
    {
        _bus = bus;
        _device = device;
        _func = func;
    }

    u16 pci_function::get_vendor_id(void)
    {
        return (_vendor_id);
    }

    u16 pci_function::get_device_id(void)
    {
        return (_device_id);
    }

    u8 pci_function::get_revision_id(void)
    {
        return (_revision_id);
    }

    bool pci_function::is_device(void)
    {
        return (_header_type == PCI_HDR_TYPE_DEVICE);
    }

    bool pci_function::is_bridge(void)
    {
        return (_header_type == PCI_HDR_TYPE_BRIDGE);
    }

    bool pci_function::is_pccard(void)
    {
        return (_header_type == PCI_HDR_TYPE_PCCARD);
    }

    bool pci_function::is_device(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type == PCI_HDR_TYPE_DEVICE);
    }

    bool pci_function::is_bridge(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type == PCI_HDR_TYPE_BRIDGE);
    }

    bool pci_function::is_pccard(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type == PCI_HDR_TYPE_PCCARD);
    }

    // Command & Status
    u16 pci_function::get_command(void)
    {
        return (pci_readw(PCI_CFG_COMMAND));
    }

    u16 pci_function::get_status(void)
    {
        return (pci_readw(PCI_CFG_STATUS));
    }

    void pci_function::set_command(u16 command)
    {
        pci_writew(PCI_CFG_COMMAND, command);
    }

    void pci_function::set_status(u16 status)
    {
        pci_writew(PCI_CFG_COMMAND, status);
    }

    bool pci_function::get_bus_master()
    {
        u16 command = get_command();
        return (command & PCI_COMMAND_BUS_MASTER);
    }

    void pci_function::set_bus_master(bool master)
    {
        u16 command = get_command();
        command =
            (master) ?
                command | PCI_COMMAND_BUS_MASTER :
                command & ~PCI_COMMAND_BUS_MASTER;
        set_command(command);
    }

    bool pci_function::is_intx_enabled(void)
    {
        u16 command = get_command();
        return ((command & PCI_COMMAND_INTX_DISABLE) == 0);
    }

    void pci_function::enable_intx(void)
    {
        u16 command = get_command();
        command &= ~PCI_COMMAND_INTX_DISABLE;
        set_command(command);
    }

    void pci_function::disable_intx(void)
    {
        u16 command = get_command();
        command |= PCI_COMMAND_INTX_DISABLE;
        set_command(command);
    }

    u8 pci_function::get_interrupt_line(void)
    {
        return (pci_readb(PCI_CFG_INTERRUPT_LINE));
    }

    void pci_function::set_interrupt_line(u8 irq)
    {
        pci_writeb(PCI_CFG_INTERRUPT_LINE, irq);
    }

    u8 pci_function::get_interrupt_pin(void)
    {
        return (pci_readb(PCI_CFG_INTERRUPT_PIN));
    }

    bool pci_function::is_msix(void)
    {
        return (_have_msix);
    }

    int pci_function::msix_get_num_entries(void)
    {
        if (!is_msix()) {
            return (0);
        }

        return (_msix.msix_msgnum);
    }

    void pci_function::msix_mask_all(void)
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl |= PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);
    }

    void pci_function::msix_unmask_all(void)
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl &= ~PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);
    }

    bool pci_function::msix_mask_entry(int entry_id)
    {
        if (!is_msix()) {
            return (false);
        }

        if (entry_id >= _msix.msix_msgnum) {
            return (false);
        }

        mmioaddr_t entryaddr = msix_get_table() + (entry_id * MSIX_ENTRY_SIZE);
        mmioaddr_t ctrl = entryaddr + (u8)MSIX_ENTRY_CONTROL;

        u32 ctrl_data = mmio_getl(ctrl);
        ctrl_data |= (1 << MSIX_ENTRY_CONTROL_MASK_BIT);
        mmio_setl(ctrl, ctrl_data);

        return (true);
    }

    bool pci_function::msix_unmask_entry(int entry_id)
    {
        if (!is_msix()) {
            return (false);
        }

        if (entry_id >= _msix.msix_msgnum) {
            return (false);
        }

        mmioaddr_t entryaddr = msix_get_table() + (entry_id * MSIX_ENTRY_SIZE);
        mmioaddr_t ctrl = entryaddr + (u8)MSIX_ENTRY_CONTROL;

        u32 ctrl_data = mmio_getl(ctrl);
        ctrl_data &= ~(1 << MSIX_ENTRY_CONTROL_MASK_BIT);
        mmio_setl(ctrl, ctrl_data);

        return (true);
    }

    bool pci_function::msix_write_entry(int entry_id, u64 address, u32 data)
    {
        if (!is_msix()) {
            return (false);
        }

        if (entry_id >= _msix.msix_msgnum) {
            return (false);
        }

        mmioaddr_t entryaddr = msix_get_table() + (entry_id * MSIX_ENTRY_SIZE);

        mmio_setq(entryaddr + (u8)MSIX_ENTRY_ADDR, address);
        mmio_setl(entryaddr + (u8)MSIX_ENTRY_DATA, data);

        return (true);
    }

    void pci_function::msix_enable(void)
    {
        if (!is_msix()) {
            return;
        }

        // mmap the msix bar into memory
        pci_bar* msix_bar = get_bar(_msix.msix_table_bar + 1);
        if (msix_bar == nullptr) {
            return;
        }

        msix_bar->map();

        // Disabled intx assertions which is turned on by default
        disable_intx();

        // Only after enabling msix, the access to the pci bar is permitted
        // so we enable it while masking all interrupts in the msix ctrl reg
        u16 ctrl = msix_get_control();
        ctrl |= PCIM_MSIXCTRL_MSIX_ENABLE;
        ctrl |= PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);

        // Mask all individual entries
        for (int i=0; i<_msix.msix_msgnum; i++) {
            msix_mask_entry(i);
        }

        // After all individual entries are masked,
        // Unmask the main block
        ctrl &= ~PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);
    }

    void pci_function::msix_disable(void)
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl &= ~PCIM_MSIXCTRL_MSIX_ENABLE;
        msix_set_control(ctrl);
    }

    void pci_function::msix_set_control(u16 ctrl)
    {
        pci_writew(_msix.msix_location + PCIR_MSIX_CTRL, ctrl);
    }

    u16 pci_function::msix_get_control(void)
    {
        return (pci_readw(_msix.msix_location + PCIR_MSIX_CTRL));
    }

    mmioaddr_t pci_function::msix_get_table(void)
    {
        pci_bar* msix_bar = get_bar(_msix.msix_table_bar + 1);
        if (msix_bar == nullptr) {
            return (mmio_nullptr);
        }

        return ( reinterpret_cast<mmioaddr_t>(msix_bar->get_mmio() +
                                              _msix.msix_table_offset) );
    }

    u8 pci_function::pci_readb(u8 offset)
    {
        return read_pci_config_byte(_bus, _device, _func, offset);
    }

    u16 pci_function::pci_readw(u8 offset)
    {
        return read_pci_config_word(_bus, _device, _func, offset);
    }

    u32 pci_function::pci_readl(u8 offset)
    {
        return read_pci_config(_bus, _device, _func, offset);
    }

    void pci_function::pci_writeb(u8 offset, u8 val)
    {
        write_pci_config_byte(_bus, _device, _func, offset, val);
    }

    void pci_function::pci_writew(u8 offset, u16 val)
    {
        write_pci_config_word(_bus, _device, _func, offset, val);
    }

    void pci_function::pci_writel(u8 offset, u32 val)
    {
        write_pci_config(_bus, _device, _func, offset, val);
    }

    u8 pci_function::find_capability(u8 cap_id)
    {
        u8 capabilities_base = pci_readb(PCI_CAPABILITIES_PTR);
        u8 off = capabilities_base;
        u8 bad_offset = 0xFF;
        u8 max_capabilities = 0xF0;
        u8 ctr = 0;

        while (off != 0) {
            // Read capability
            u8 capability = pci_readb(off + PCI_CAP_OFF_ID);
            if (capability == cap_id) {
                return (off);
            }

            ctr++;
            if (ctr > max_capabilities) {
                return (bad_offset);
            }

            // Next
            off = pci_readb(off + PCI_CAP_OFF_NEXT);
        }

        return (bad_offset);
    }

    pci_bar * pci_function::get_bar(int idx)
    {
        auto it = _bars.find(idx);
        if (it == _bars.end()) {
            return (nullptr);
        }

        return (it->second);
    }

    void pci_function::add_bar(int idx, pci_bar * bar)
    {
        _bars.insert(std::make_pair(idx, bar));
    }

    void pci_function::dump_config(void)
    {
        debug(fmt("[%x:%x.%x] vid:id = %x:%x") %
            (u16)_bus % (u16)_device % (u16)_func % _vendor_id % _device_id);

        // PCI BARs
        int bar_idx = 1;
        pci_bar *bar = get_bar(bar_idx);
        while (bar != nullptr) {
            debug(fmt("    bar[%d]: %sbits addr=%x size=%x") % bar_idx %
                (bar->is_64()?"64":"32") % bar->get_addr64() % bar->get_size());
            bar = get_bar(++bar_idx);
        }

        debug(fmt("    IRQ = %d") % (u16)get_interrupt_line());

        // MSI-x
        if (_have_msix) {
            debug(fmt("    Have MSI-X!"));
            debug(fmt("        msix_location: %1%") % (u16)_msix.msix_location);
            debug(fmt("        msix_ctrl: %1%") % _msix.msix_ctrl);
            debug(fmt("        msix_msgnum: %1%") % _msix.msix_msgnum);
            debug(
                fmt("        msix_table_bar: %1%") % (u16)_msix.msix_table_bar);
            debug(
                fmt("        msix_table_offset: %1%")
                    % _msix.msix_table_offset);
            debug(fmt("        msix_pba_bar: %1%") % (u16)_msix.msix_pba_bar);
            debug(fmt("        msix_pba_offset: %1%") % _msix.msix_pba_offset);
        }
    }
}
