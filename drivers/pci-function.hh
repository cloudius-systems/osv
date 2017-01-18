/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef PCI_FUNCTION_H
#define PCI_FUNCTION_H

#include <map>
#include <ostream>

#include <osv/mmio.hh>
#include <osv/types.h>
#include "processor.hh"
#include <osv/debug.hh>
#include "device.hh"

using namespace hw;

namespace pci {

    class function;

    class bar {
    public:

        enum pci_bar_encoding_masks {
            // mmio, pio
            PCI_BAR_MEMORY_INDICATOR_MASK   = (1 << 0),
            // 32bit, 64bit
            PCI_BAR_MEM_ADDR_SPACE_MASK     = (1 << 1) | (1 << 2),
            PCI_BAR_PREFETCHABLE_MASK       = (1 << 3),
            PCI_BAR_MEM_ADDR_LO_MASK        = 0xFFFFFFF0,
            PCI_BAR_PIO_ADDR_MASK           = 0xFFFFFFFC,
        };

        enum pci_bar_type_indicator {
            PCI_BAR_MMIO                = 0x00,
            PCI_BAR_PIO                 = 0x01
        };

        enum pci_bar_prefetchable {
            PCI_BAR_NON_PREFETCHABLE    = 0x00,
            PCI_BAR_PREFETCHABLE        = 0x08
        };

        enum pci_bar_address_space {
            PCI_BAR_32BIT_ADDRESS       = 0x00,
            PCI_BAR_32BIT_BELOW_1MB     = 0x02,
            PCI_BAR_64BIT_ADDRESS       = 0x04
        };

        // pos is the offset within the configuration space
        bar(function* dev, u8 pos);
        virtual ~bar();

        // read/write pci cfg registers to determine size
        u64 read_bar_size();

        bool is_pio() { return !_is_mmio; }
        bool is_mmio() { return _is_mmio; }
        bool is_32() { return !_is_64; }
        bool is_64() { return _is_64; }
        bool is_prefetchable() { return _is_prefetchable; }

        u32 get_addr_lo() { return _addr_lo; }
        u32 get_addr_hi() { return _addr_hi; }
        u64 get_addr64() { return _addr_64; }
        u64 get_size() { return _addr_size; }

        // map mmio region
        void map();
        void unmap();
        mmioaddr_t get_mmio();

        // Access the pio or mmio bar
        u64 readq(u32 offset);
        u32 readl(u32 offset);
        u16 readw(u32 offset);
        u8 readb(u32 offset);
        void writeq(u32 offset, u64 val);
        void writel(u32 offset, u32 val);
        void writew(u32 offset, u16 val);
        void writeb(u32 offset, u8 val);

    private:

        void init();

        /* Architecture-specific hook on bar creation, which allows
         * rewriting the bar registers. Returns the bar register.
         */
        u32 arch_add_bar(u32 val);

        // To which pci_function it relates
        function* _dev;
        // Offset to configuration space
        u8 _pos;
        // Base address
        u32 _addr_lo, _addr_hi;
        u64 _addr_64;
        u64 _addr_size;
        mmioaddr_t _addr_mmio;

        bool _is_mmio;
        bool _is_64;
        bool _is_prefetchable;
    };

    //  MSI-X definitions
    enum msi_pci_conf {
        PCIR_MSI_CTRL       = 0x2,
        PCIR_MSI_CTRL_ME    = 1 << 0,
        PCIR_MSI_ADDR       = 0x4,
        PCIR_MSI_UADDR      = 0x8,
        PCIR_MSI_DATA_32    = 0x8,
        PCIR_MSI_DATA_64    = 0x0C,
        PCIR_MSI_MASK_32    = 0x0C,
        PCIR_MSI_MASK_64    = 0x10,
    };

    //  MSI-X definitions
    enum msix_pci_conf {
        PCIR_MSIX_CTRL = 0x2,
        PCIM_MSIXCTRL_MSIX_ENABLE = 0x8000,
        PCIM_MSIXCTRL_FUNCTION_MASK = 0x4000,
        PCIM_MSIXCTRL_TABLE_SIZE = 0x07FF,
        PCIR_MSIX_TABLE = 0x4,
        PCIR_MSIX_PBA = 0x8,
        PCIM_MSIX_BIR_MASK = 0x7,

        // Entry offsets
        MSIX_ENTRY_ADDR             = 0,
        MSIX_ENTRY_ADDR_LO          = 0,
        MSIX_ENTRY_ADDR_HI          = 4,
        MSIX_ENTRY_DATA             = 8,
        MSIX_ENTRY_CONTROL          = 12,
        MSIX_ENTRY_SIZE             = 16,
        MSIX_ENTRY_CONTROL_MASK_BIT = 0
    };

    struct pcicfg_msix {
        u16 msix_ctrl;                          //  Message Control
        u16 msix_msgnum;                        //  Number of messages
        u8 msix_location;                       //  Offset of MSI-X capability registers.
        u8 msix_table_bar;                      //  BAR containing vector table.
        u8 msix_pba_bar;                        //  BAR containing PBA.
        u32 msix_table_offset;
        u32 msix_pba_offset;
    };

    struct pcicfg_msi {
        u8 msi_location;                       //  Offset of MSI capability registers.
        u16 msi_ctrl;                          //  Message Control
        u16 msi_msgnum;                        //  Number of messages
        bool is_64_address;                    //  64 bit address
        bool is_vector_mask;                   //  Per-vector mask
    };

    // Represents a PCI function (pci device, bridge or virtio device)
    class function : public hw_device {
    public:

        enum pci_function_cfg_offsets {
            PCI_CFG_VENDOR_ID             = 0x00,
            PCI_CFG_DEVICE_ID             = 0x02,
            PCI_CFG_COMMAND             = 0x04,
            PCI_CFG_STATUS                 = 0x06,
            PCI_CFG_REVISION_ID         = 0x08,
            PCI_CFG_CLASS_CODE0            = 0x0B,
            PCI_CFG_CLASS_CODE1            = 0x0A,
            PCI_CFG_CLASS_CODE2            = 0x09,
            PCI_CFG_CACHELINE_SIZE        = 0x0C,
            PCI_CFG_LATENCY_TIMER         = 0x0D,
            PCI_CFG_HEADER_TYPE         = 0x0E,
            PCI_CFG_BIST                 = 0x0F,
            PCI_CFG_BAR_1                 = 0x10,
            PCI_CFG_BAR_2                 = 0x14,
            PCI_CFG_BAR_3                 = 0x18,
            PCI_CFG_BAR_4                 = 0x1C,
            PCI_CFG_BAR_5                 = 0x20,
            PCI_CFG_BAR_6                 = 0x24,
            PCI_CFG_CARDBUS_CIS_PTR     = 0x28,
            PCI_CFG_SUBSYSTEM_VENDOR_ID = 0x2C,
            PCI_CFG_SUBSYSTEM_ID         = 0x2E,
            PCI_CFG_CAPABILITIES_PTR     = 0x34,
            PCI_CFG_INTERRUPT_LINE         = 0x3C,
            PCI_CFG_INTERRUPT_PIN        = 0x3D
        };

        enum pci_command_bits {
            PCI_COMMAND_INTX_DISABLE = (1 << 10),
            PCI_COMMAND_BUS_MASTER = (1 << 2),
            PCI_COMMAND_BAR_MEM_ENABLE = (1 << 1),
            PCI_COMMAND_BAR_IO_ENABLE = (1 << 0)
        };

        enum pci_header_type {
            PCI_HDR_TYPE_DEVICE = 0x00,
            PCI_HDR_TYPE_BRIDGE = 0x01,
            PCI_HDR_TYPE_PCCARD = 0x02,
            PCI_HDR_TYPE_MASK   = 0x03
        };

        //  Capability Register Offsets
        enum pci_capabilities_offsets {
            PCI_CAP_OFF_ID      = 0x0,
            PCI_CAP_OFF_NEXT    = 0x1
        };

        enum pci_capabilities {
            PCI_CAP_PM          = 0x01,    // PCI Power Management
            PCI_CAP_AGP         = 0x02,    // AGP
            PCI_CAP_VPD         = 0x03,    // Vital Product Data
            PCI_CAP_SLOTID      = 0x04,    // Slot Identification
            PCI_CAP_MSI         = 0x05,    // Message Signaled Interrupts
            PCI_CAP_CHSWP       = 0x06,    // CompactPCI Hot Swap
            PCI_CAP_PCIX        = 0x07,    // PCI-X
            PCI_CAP_HT          = 0x08,    // HyperTransport
            PCI_CAP_VENDOR      = 0x09,    // Vendor Unique
            PCI_CAP_DEBUG       = 0x0a,    // Debug port
            PCI_CAP_CRES        = 0x0b,    // CompactPCI central resource control
            PCI_CAP_HOTPLUG     = 0x0c,    // PCI Hot-Plug
            PCI_CAP_SUBVENDOR   = 0x0d,    // PCI-PCI bridge subvendor ID
            PCI_CAP_AGP8X       = 0x0e,    // AGP 8x
            PCI_CAP_SECDEV      = 0x0f,    // Secure Device
            PCI_CAP_EXPRESS     = 0x10,    // PCI Express
            PCI_CAP_MSIX        = 0x11,    // MSI-X
            PCI_CAP_SATA        = 0x12,    // SATA
            PCI_CAP_PCIAF       = 0x13     // PCI Advanced Features
        };

        enum pci_class_codes {
            PCI_CLASS_STORAGE       = 0x01,
            PCI_CLASS_DISPLAY       = 0x03
        };

        enum pci_sub_class_codes {
            PCI_SUB_CLASS_STORAGE_SCSI      = 0x00,
            PCI_SUB_CLASS_STORAGE_IDE       = 0x01,
            PCI_SUB_CLASS_STORAGE_FLOPPY    = 0x02,
            PCI_SUB_CLASS_STORAGE_IPI       = 0x03,
            PCI_SUB_CLASS_STORAGE_RAID      = 0x04,
            PCI_SUB_CLASS_STORAGE_ATA       = 0x05,
            PCI_SUB_CLASS_STORAGE_AHCI      = 0x06,
            PCI_SUB_CLASS_STORAGE_SAS       = 0x07,
            PCI_SUB_CLASS_STORAGE_NVMC      = 0x08,
        };

        function(u8 bus, u8 device, u8 func);
        virtual ~function();

        // Implement device interface
        virtual hw_device_id get_id();
        virtual void print();
        virtual void reset();

        // Parses the PCI configuration space
        virtual bool parse_pci_config();

        // Position
        void get_bdf(u8& bus, u8 &device, u8& func);
        void set_bdf(u8 bus, u8 device, u8 func);

        // Identification
        u16 get_vendor_id();
        u16 get_device_id();
        u8 get_revision_id();

        u8 get_base_class_code();
        u8 get_sub_class_code();
        u8 get_programming_interface();

        // Type
        bool is_device();
        bool is_bridge();
        bool is_pccard();

        static bool is_device(u8 bus, u8 device, u8 function);
        static bool is_bridge(u8 bus, u8 device, u8 function);
        static bool is_pccard(u8 bus, u8 device, u8 function);

        // Command & Status
        u16 get_command();
        u16 get_status();
        void set_command(u16 command);
        void set_status(u16 status);

        bool get_bus_master();
        void set_bus_master(bool m);

        void set_bars_enable(bool mem, bool io);

        // Enable/Disable intx assertions
        bool is_intx_enabled();
        // Enable intx assertion
        // intx assertions should be disabled in order to use MSI-x
        void enable_intx();
        void disable_intx();

        // Interrupts (PCI configuration)
        u8 get_interrupt_line();
        void set_interrupt_line(u8 irq);
        u8 get_interrupt_pin();

        // Does this device support MSI-x
        bool is_msix();
        unsigned msix_get_num_entries();
        void msix_mask_all();
        void msix_unmask_all();
        bool msix_mask_entry(int entry_id);
        bool msix_unmask_entry(int entry_id);
        bool msix_write_entry(int entry_id, u64 address, u32 data);

        // Enable MSIx, start with all vectors masked
        void msix_enable();
        // Good for reset maybe, call disable and enable
        void msix_disable();
        bool is_msix_enabled() {return _msix_enabled;}

        // MSI
        // Does this device support MSI
        bool is_msi();
        unsigned msi_get_num_entries();
        void msi_mask_all();
        void msi_unmask_all();
        bool msi_mask_entry(int entry_id);
        bool msi_unmask_entry(int entry_id);
        bool msi_write_entry(int entry_id, u64 address, u32 data);
        void msi_enable();
        void msi_disable();
        bool is_msi_enabled() {return _msi_enabled;}

        // Access to PCI address space
        virtual u8 pci_readb(u8 offset);
        virtual u16 pci_readw(u8 offset);
        virtual u32 pci_readl(u8 offset);
        virtual void pci_writeb(u8 offset, u8 val);
        virtual void pci_writew(u8 offset, u16 val);
        virtual void pci_writel(u8 offset, u32 val);

        // Capability parsing
        u8 find_capability(u8 cap_id);

        bar * get_bar(int idx);
        void add_bar(int idx, bar* bar);

        // Useful function to print device
        virtual void dump_config();

        friend std::ostream& operator << (std::ostream& out, const function &d);
        struct equal {
            bool operator()(const function* d1, const function* d2) const
            {
                return d1->_device_id == d2->_device_id && d1->_vendor_id == d2->_vendor_id;
            }
        };

        struct hash : std::unary_function< const function*, std::size_t> {
            std::size_t operator() ( const function* const key ) const {
                return (size_t)((key->_device_id<<16)+ key->_vendor_id);
            }
        };

    protected:
        // Parsing of extra capabilities
        virtual bool parse_pci_capabilities();
        virtual bool parse_pci_msix(u8 off);
        virtual bool parse_pci_msi(u8 off);

        // Don't call if msix capability is not present
        void msix_set_control(u16 ctrl);
        u16 msix_get_control();
        mmioaddr_t msix_get_table();

        void msi_set_control(u16 ctrl);
        u16 msi_get_control();

        // Position
        u8  _bus, _device, _func;

        // Some fields that are parsed from the configuration space
        u16 _device_id;
        u16 _vendor_id;
        u8 _revision_id;
        u8 _header_type;
        u8 _base_class_code;
        u8 _sub_class_code;
        u8 _programming_interface;

        // Index -> PCI Bar
        std::map<int, bar *> _bars;

        // MSI-x
        bool _have_msix;
        pcicfg_msix _msix;
        bool _msix_enabled;

        // MSI-x
        bool _have_msi;
        pcicfg_msi _msi;
        bool _msi_enabled;
    };
}

#endif // PCI_DEVICE_H
