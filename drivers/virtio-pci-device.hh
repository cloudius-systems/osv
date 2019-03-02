/*
 * Copyright (C) 2019 Waldemar Kozaczuk.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_PCI_DEVICE_HH
#define VIRTIO_PCI_DEVICE_HH

#include <osv/pci.hh>
#include <osv/interrupt.hh>
#include <osv/msi.hh>

#include "virtio-device.hh"
#include "virtio.hh"

namespace virtio {

enum VIRTIO_PCI_CONFIG {
    /* A 32-bit r/o bitmask of the features supported by the host */
    VIRTIO_PCI_HOST_FEATURES = 0,
    /* A 32-bit r/o bitmask of the features supported by the host */
    VIRTIO_PCI_HOST_FEATURES_HIGH = 1,
    /* A 32-bit r/w bitmask of features activated by the guest */
    VIRTIO_PCI_GUEST_FEATURES = 4,
    /* A 32-bit r/w PFN for the currently selected queue */
    VIRTIO_PCI_QUEUE_PFN = 8,
    /* A 16-bit r/o queue size for the currently selected queue */
    VIRTIO_PCI_QUEUE_NUM = 12,
    /* A 16-bit r/w queue selector */
    VIRTIO_PCI_QUEUE_SEL = 14,
    /* A 16-bit r/w queue notifier */
    VIRTIO_PCI_QUEUE_NOTIFY = 16,
    /* An 8-bit device status register.  */
    VIRTIO_PCI_STATUS = 18,
    /* An 8-bit r/o interrupt status register.  Reading the value will return the
     * current contents of the ISR and will also clear it.  This is effectively
     * a read-and-acknowledge. */
    VIRTIO_PCI_ISR = 19,
    /* The bit of the ISR which indicates a device configuration change. */
    VIRTIO_PCI_ISR_CONFIG  = 0x2,
    /* MSI-X registers: only enabled if MSI-X is enabled. */
    /* A 16-bit vector for configuration changes. */
    VIRTIO_MSI_CONFIG_VECTOR = 20,
    /* A 16-bit vector for selected queue notifications. */
    VIRTIO_MSI_QUEUE_VECTOR = 22,
    /* Vector value used to disable MSI for queue */
    VIRTIO_MSI_NO_VECTOR = 0xffff,
    /* Virtio ABI version, this must match exactly */
    VIRTIO_PCI_LEGACY_ABI_VERSION = 0,
    /* How many bits to shift physical queue address written to QUEUE_PFN.
     * 12 is historical, and due to x86 page size. */
    VIRTIO_PCI_QUEUE_ADDR_SHIFT = 12,
    /* The alignment to use between consumer and producer parts of vring.
     * x86 pagesize again. */
    VIRTIO_PCI_VRING_ALIGN = 4096,
    VIRTIO_PCI_LEGACY_ID_MIN = 0x1000,
    VIRTIO_PCI_LEGACY_ID_MAX = 0x103f,
    VIRTIO_PCI_MODERN_ABI_VERSION = 1,
    VIRTIO_PCI_MODERN_ID_MIN = 0x1040,
    VIRTIO_PCI_MODERN_ID_MAX = 0x107f,
};

class virtio_pci_device : public virtio_device {
public:
    explicit virtio_pci_device(pci::device *dev);
    ~virtio_pci_device();

    virtual const char *get_version() = 0;
    virtual u16 get_type_id() = 0;

    virtual hw_device_id get_id() { return hw_device_id(VIRTIO_VENDOR_ID,get_type_id()); }
    virtual void print() { _dev->print(); }
    virtual void reset() { _dev->reset(); }

    bool is_attached()  { return _dev->is_attached(); }
    void set_attached() { _dev->set_attached(); }

    virtual void dump_config();
    virtual void init();
    virtual void register_interrupt(interrupt_factory irq_factory);

    virtual unsigned get_irq() { return 0; }
    size_t get_vring_alignment() { return VIRTIO_PCI_VRING_ALIGN; }

protected:
    virtual bool parse_pci_config() = 0;

    pci::device *_dev;
    interrupt_manager _msi;
    std::unique_ptr<pci_interrupt> _irq;
};

class virtio_legacy_pci_device : public virtio_pci_device {
public:
    explicit virtio_legacy_pci_device(pci::device *dev);
    ~virtio_legacy_pci_device() {}

    virtual const char *get_version() { return "legacy"; }
    virtual u16 get_type_id() { return _dev->get_subsystem_id(); };

    virtual void select_queue(int queue);
    virtual u16 get_queue_size();
    virtual void setup_queue(vring *queue);
    virtual void activate_queue(int queue) {}
    virtual void kick_queue(int queue);

    virtual u64 get_available_features();
    virtual void set_enabled_features(u64 features);

    virtual u8 get_status();
    virtual void set_status(u8 status);

    virtual u8 read_config(u32 offset);
    virtual u8 read_and_ack_isr();

    virtual bool is_modern() { return false; };
protected:
    virtual bool parse_pci_config();

private:
    // Access the virtio conf address space set by pci bar 1
    bool get_virtio_config_bit(u32 offset, int bit);

    // Access virtio config space
    u8 virtio_conf_readb(u32 offset) { return _bar1->readb(offset);};
    u16 virtio_conf_readw(u32 offset) { return _bar1->readw(offset);};
    u32 virtio_conf_readl(u32 offset) { return _bar1->readl(offset);};
    void virtio_conf_writeb(u32 offset, u8 val) { _bar1->writeb(offset, val);};
    void virtio_conf_writew(u32 offset, u16 val) { _bar1->writew(offset, val);};
    void virtio_conf_writel(u32 offset, u32 val) { _bar1->writel(offset, val);};

    pci::bar* _bar1;
};

enum VIRTIO_MODERN_PCI_CONFIG {
    /* Common configuration */
    VIRTIO_PCI_CAP_COMMON_CFG = 1,
    /* Notifications */
    VIRTIO_PCI_CAP_NOTIFY_CFG = 2,
    /* ISR Status */
    VIRTIO_PCI_CAP_ISR_CFG = 3,
    /* Device specific configuration */
    VIRTIO_PCI_CAP_DEVICE_CFG = 4,
    /* PCI configuration access */
    VIRTIO_PCI_CAP_PCI_CFG = 5,
};

/* This is the PCI capability header: */
struct virtio_pci_cap {
    u8 cap_vndr;    /* Generic PCI field: PCI_CAP_ID_VNDR */
    u8 cap_next;    /* Generic PCI field: next ptr. */
    u8 cap_len;     /* Generic PCI field: capability length */
    u8 cfg_type;    /* Identifies the structure. */
    u8 bar;         /* Where to find it. */
    u8 padding[3];  /* Pad to full dword. */
    u32 offset;     /* Offset within bar. */
    u32 length;     /* Length of the structure, in bytes. */
};

/* The notification location is found using the VIRTIO_PCI_CAP_NOTIFY_CFG capability.
 * This capability is immediately followed by an additional field, like so:*/
struct virtio_pci_notify_cap {
    struct virtio_pci_cap cap;
    u32 notify_offset_multiplier;  /* Multiplier for queue_notify_off. */
};

/* The common configuration structure is found at the bar and offset within
 * the VIRTIO_PCI_CAP_COMMON_CFG capability; its layout is below. */
struct virtio_pci_common_cfg {
    /* About the whole device. */
    u32 device_feature_select;     /* read-write */
    u32 device_feature;            /* read-only for driver */
    u32 driver_feature_select;     /* read-write */
    u32 driver_feature;            /* read-write */
    u16 msix_config;               /* read-write */
    u16 num_queues;                /* read-only for driver */
    u8 device_status;              /* read-write */
    u8 config_generation;          /* read-only for driver */

    /* About a specific virtqueue. */
    u16 queue_select;              /* read-write */
    u16 queue_size;                /* read-write, power of 2, or 0. */
    u16 queue_msix_vector;         /* read-write */
    u16 queue_enable;              /* read-write */
    u16 queue_notify_off;          /* read-only for driver */
    u32 queue_desc_lo;             /* read-write */
    u32 queue_desc_hi;             /* read-write */
    u32 queue_avail_lo;            /* read-write */
    u32 queue_avail_hi;            /* read-write */
    u32 queue_used_lo;             /* read-write */
    u32 queue_used_hi;             /* read-write */
};

#define COMMON_CFG_OFFSET_OF(field) offsetof(struct virtio_pci_common_cfg, field)

class virtio_modern_pci_device : public virtio_pci_device {
public:
    struct virtio_capability {
        virtio_capability(u32 cfg_offset, pci::bar* bar, u32 bar_no, u32 bar_offset, u32 length) :
            _cfg_offset(cfg_offset),
            _bar(bar),
            _bar_no(bar_no),
            _bar_offset(bar_offset),
            _length(length) {
            assert(_length > 0 && _bar_offset >= 0 && _bar_offset + _length <= _bar->get_size());
        }

        u8 virtio_conf_readb(u32 offset) {
            verify_offset(offset, sizeof(u8));
            return _bar->readb(_bar_offset + offset);
        };
        u16 virtio_conf_readw(u32 offset) {
            verify_offset(offset, sizeof(u16));
            return _bar->readw(_bar_offset + offset);
        };
        u32 virtio_conf_readl(u32 offset) {
            verify_offset(offset, sizeof(u32));
            return _bar->readl(_bar_offset + offset);
        };
        void virtio_conf_writeb(u32 offset, u8 val) {
            verify_offset(offset, sizeof(u8));
            _bar->writeb(_bar_offset + offset, val);
        };
        void virtio_conf_writew(u32 offset, u16 val) {
            verify_offset(offset, sizeof(u16));
            _bar->writew(_bar_offset + offset, val);
        };
        void virtio_conf_writel(u32 offset, u32 val) {
            verify_offset(offset, sizeof(u32));
            _bar->writel(_bar_offset + offset, val);
        };
        u32 get_cfg_offset() { return _cfg_offset; }

        void print(const char *prefix) {
            virtio_d("%s bar=%d, offset=%x, size=%x", prefix, _bar_no, _bar_offset, _length);
        }
    private:
        inline void verify_offset(u32 offset, u32 size) {
            assert(offset >= 0 && offset + size <= _length);
        }

        u32 _cfg_offset;
        pci::bar* _bar;
        u32 _bar_no;
        u32 _bar_offset;
        u32 _length;
    };

    explicit virtio_modern_pci_device(pci::device *dev);
    ~virtio_modern_pci_device() {}

    virtual const char *get_version() { return "modern"; }
    virtual u16 get_type_id() { return _dev->get_device_id() - VIRTIO_PCI_MODERN_ID_MIN; };

    virtual void dump_config();

    virtual void select_queue(int queue);
    virtual u16 get_queue_size();
    virtual void setup_queue(vring *queue);
    virtual void activate_queue(int queue);
    virtual void kick_queue(int queue);

    virtual u64 get_available_features();
    virtual void set_enabled_features(u64 features);

    virtual u8 get_status();
    virtual void set_status(u8 status);

    virtual u8 read_config(u32 offset);
    virtual u8 read_and_ack_isr();

    virtual bool is_modern() { return true; };

protected:
    virtual bool parse_pci_config();
private:
    void parse_virtio_capability(std::unique_ptr<virtio_capability> &ptr, u8 type);

    std::unique_ptr<virtio_capability> _common_cfg;
    std::unique_ptr<virtio_capability> _isr_cfg;
    std::unique_ptr<virtio_capability> _notify_cfg;
    std::unique_ptr<virtio_capability> _device_cfg;

    u32 _notify_offset_multiplier;
    u32 _queues_notify_offsets[64];
};

// Creates appropriate instance of virtio_pci_device
// by reading configuration from PCI device
virtio_device* create_virtio_pci_device(pci::device *dev);

}

#endif //VIRTIO_PCI_DEVICE_HH