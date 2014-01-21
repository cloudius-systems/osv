/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.hh>
#include <osv/mmio.hh>
#include "pci.hh"
#include "device.hh"
#include "pci-function.hh"

using namespace hw;

namespace pci {

    bar::bar(function* dev, u8 pos)
        : _dev(dev), _pos(pos),
          _addr_lo(0), _addr_hi(0), _addr_64(0), _addr_size(0),
          _addr_mmio(mmio_nullptr),
          _is_mmio(false), _is_64(false), _is_prefetchable(false)
    {
        init();
    }

    bar::~bar()
    {

    }

    void bar::init(void)
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

    void bar::test_bar_size(void)
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

    void bar::map(void)
    {
        if (_is_mmio) {
            _addr_mmio = mmio_map(get_addr64(), get_size());
        }
    }

    void bar::unmap(void)
    {
        if ((_is_mmio) && (_addr_mmio != mmio_nullptr)) {
            mmio_unmap(_addr_mmio, get_size());
        }
    }

    mmioaddr_t bar::get_mmio(void)
    {
        return (_addr_mmio);
    }

    u32 bar::readl(u32 offset)
    {
        if (is_pio()) {
            return (inl(_addr_lo + offset));
        } else {
            return mmio_getl(_addr_mmio + offset);
        }
    }

    u16 bar::readw(u32 offset)
    {
        if (is_pio()) {
            return (inw(_addr_lo + offset));
        } else {
            return mmio_getw(_addr_mmio + offset);
        }
    }

    u8 bar::readb(u32 offset)
    {
        if (is_pio()) {
            return (inb(_addr_lo + offset));
        } else {
            return mmio_getb(_addr_mmio + offset);
        }
    }

    void bar::writel(u32 offset, u32 val)
    {
        if (is_pio()) {
            outl(val, _addr_lo + offset);
        } else {
            return mmio_setl(_addr_mmio + offset, val);
        }
    }

    void bar::writew(u32 offset, u16 val)
    {
        if (is_pio()) {
            outw(val, _addr_lo + offset);
        } else {
            return mmio_setw(_addr_mmio + offset, val);
        }
    }

    void bar::writeb(u32 offset, u8 val)
    {
        if (is_pio()) {
            outb(val, _addr_lo + offset);
        } else {
            return mmio_setb(_addr_mmio + offset, val);
        }
    }

    function::function(u8 bus, u8 device, u8 func)
        : _bus(bus), _device(device), _func(func), _have_msix(false), _msix_enabled(false)
    {

    }

    function::~function()
    {
        for (auto it = _bars.begin(); it != _bars.end(); it++) {
            delete (it->second);
        }
    }

    hw_device_id function::get_id(void)
    {
        return (hw_device_id(_vendor_id, _device_id));
    }

    void function::print(void)
    {
        dump_config();
    }

    void function::reset(void)
    {
        // TODO: implement
    }

    bool function::parse_pci_config(void)
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

    bool function::parse_pci_capabilities(void)
    {
        // Parse MSI-X
        u8 off = find_capability(PCI_CAP_MSIX);
        if (off != 0xFF) {
            bool msi_ok = parse_pci_msix(off);
            return (msi_ok);
        }

        return true;
    }

    bool function::parse_pci_msix(u8 off)
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

    void function::get_bdf(u8& bus, u8 &device, u8& func)
    {
        bus = _bus;
        device = _device;
        func = _func;
    }

    void function::set_bdf(u8 bus, u8 device, u8 func)
    {
        _bus = bus;
        _device = device;
        _func = func;
    }

    u16 function::get_vendor_id(void)
    {
        return (_vendor_id);
    }

    u16 function::get_device_id(void)
    {
        return (_device_id);
    }

    u8 function::get_revision_id(void)
    {
        return (_revision_id);
    }

    bool function::is_device(void)
    {
        return (_header_type == PCI_HDR_TYPE_DEVICE);
    }

    bool function::is_bridge(void)
    {
        return (_header_type == PCI_HDR_TYPE_BRIDGE);
    }

    bool function::is_pccard(void)
    {
        return (_header_type == PCI_HDR_TYPE_PCCARD);
    }

    bool function::is_device(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type == PCI_HDR_TYPE_DEVICE);
    }

    bool function::is_bridge(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type == PCI_HDR_TYPE_BRIDGE);
    }

    bool function::is_pccard(u8 bus, u8 device, u8 function)
    {
        u8 header_type = read_pci_config_byte(bus, device, function,
            PCI_CFG_HEADER_TYPE);
        return (header_type == PCI_HDR_TYPE_PCCARD);
    }

    // Command & Status
    u16 function::get_command(void)
    {
        return (pci_readw(PCI_CFG_COMMAND));
    }

    u16 function::get_status(void)
    {
        return (pci_readw(PCI_CFG_STATUS));
    }

    void function::set_command(u16 command)
    {
        pci_writew(PCI_CFG_COMMAND, command);
    }

    void function::set_status(u16 status)
    {
        pci_writew(PCI_CFG_COMMAND, status);
    }

    bool function::get_bus_master()
    {
        u16 command = get_command();
        return (command & PCI_COMMAND_BUS_MASTER);
    }

    void function::set_bus_master(bool master)
    {
        u16 command = get_command();
        command =
            (master) ?
                command | PCI_COMMAND_BUS_MASTER :
                command & ~PCI_COMMAND_BUS_MASTER;
        set_command(command);
    }

    bool function::is_intx_enabled(void)
    {
        u16 command = get_command();
        return ((command & PCI_COMMAND_INTX_DISABLE) == 0);
    }

    void function::enable_intx(void)
    {
        u16 command = get_command();
        command &= ~PCI_COMMAND_INTX_DISABLE;
        set_command(command);
    }

    void function::disable_intx(void)
    {
        u16 command = get_command();
        command |= PCI_COMMAND_INTX_DISABLE;
        set_command(command);
    }

    u8 function::get_interrupt_line(void)
    {
        return (pci_readb(PCI_CFG_INTERRUPT_LINE));
    }

    void function::set_interrupt_line(u8 irq)
    {
        pci_writeb(PCI_CFG_INTERRUPT_LINE, irq);
    }

    u8 function::get_interrupt_pin(void)
    {
        return (pci_readb(PCI_CFG_INTERRUPT_PIN));
    }

    bool function::is_msix(void)
    {
        return (_have_msix);
    }

    unsigned function::msix_get_num_entries(void)
    {
        if (!is_msix()) {
            return (0);
        }

        return (_msix.msix_msgnum);
    }

    void function::msix_mask_all(void)
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl |= PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);
    }

    void function::msix_unmask_all(void)
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl &= ~PCIM_MSIXCTRL_FUNCTION_MASK;
        msix_set_control(ctrl);
    }

    bool function::msix_mask_entry(int entry_id)
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

    bool function::msix_unmask_entry(int entry_id)
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

    bool function::msix_write_entry(int entry_id, u64 address, u32 data)
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

    void function::msix_enable(void)
    {
        if (!is_msix()) {
            return;
        }

        // mmap the msix bar into memory
        bar* msix_bar = get_bar(_msix.msix_table_bar + 1);
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

        _msix_enabled = true;
    }

    void function::msix_disable(void)
    {
        if (!is_msix()) {
            return;
        }

        u16 ctrl = msix_get_control();
        ctrl &= ~PCIM_MSIXCTRL_MSIX_ENABLE;
        msix_set_control(ctrl);

        _msix_enabled = false;
    }

    void function::msix_set_control(u16 ctrl)
    {
        pci_writew(_msix.msix_location + PCIR_MSIX_CTRL, ctrl);
    }

    u16 function::msix_get_control(void)
    {
        return (pci_readw(_msix.msix_location + PCIR_MSIX_CTRL));
    }

    mmioaddr_t function::msix_get_table(void)
    {
        bar* msix_bar = get_bar(_msix.msix_table_bar + 1);
        if (msix_bar == nullptr) {
            return (mmio_nullptr);
        }

        return ( reinterpret_cast<mmioaddr_t>(msix_bar->get_mmio() +
                                              _msix.msix_table_offset) );
    }

    u8 function::pci_readb(u8 offset)
    {
        return read_pci_config_byte(_bus, _device, _func, offset);
    }

    u16 function::pci_readw(u8 offset)
    {
        return read_pci_config_word(_bus, _device, _func, offset);
    }

    u32 function::pci_readl(u8 offset)
    {
        return read_pci_config(_bus, _device, _func, offset);
    }

    void function::pci_writeb(u8 offset, u8 val)
    {
        write_pci_config_byte(_bus, _device, _func, offset, val);
    }

    void function::pci_writew(u8 offset, u16 val)
    {
        write_pci_config_word(_bus, _device, _func, offset, val);
    }

    void function::pci_writel(u8 offset, u32 val)
    {
        write_pci_config(_bus, _device, _func, offset, val);
    }

    u8 function::find_capability(u8 cap_id)
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

    bar * function::get_bar(int idx)
    {
        auto it = _bars.find(idx);
        if (it == _bars.end()) {
            return (nullptr);
        }

        return (it->second);
    }

    void function::add_bar(int idx, bar * bar)
    {
        _bars.insert(std::make_pair(idx, bar));
    }

    void function::dump_config(void)
    {
        pci_d("[%x:%x.%x] vid:id = %x:%x",
            (u16)_bus, (u16)_device, (u16)_func, _vendor_id, _device_id);

        // PCI BARs
        int bar_idx = 1;
        bar *bar = get_bar(bar_idx);
        while (bar != nullptr) {
            pci_d("    bar[%d]: %sbits addr=%x size=%x", bar_idx,
                (bar->is_64()?"64":"32"), bar->get_addr64(), bar->get_size());
            bar = get_bar(++bar_idx);
        }

        pci_d("    IRQ = %d", (u16)get_interrupt_line());

        // MSI-x
        if (_have_msix) {
            pci_d("    Have MSI-X!");
            pci_d("        msix_location: %d", (u16)_msix.msix_location);
            pci_d("        msix_ctrl: %d", _msix.msix_ctrl);
            pci_d("        msix_msgnum: %d", _msix.msix_msgnum);
            pci_d("        msix_table_bar: %d", (u16)_msix.msix_table_bar);
            pci_d("        msix_table_offset: %d", _msix.msix_table_offset);
            pci_d("        msix_pba_bar: %d", (u16)_msix.msix_pba_bar);
            pci_d("        msix_pba_offset: %d", _msix.msix_pba_offset);
        }
    }
}
