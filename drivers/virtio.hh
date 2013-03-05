#ifndef VIRTIO_DRIVER_H
#define VIRTIO_DRIVER_H

#include "driver.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include "drivers/virtio-vring.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-device.hh"
#include "drivers/virtio-vring.hh"
#include "interrupt.hh"

namespace virtio {

enum VIRTIO_CONFIG {
    /* Status byte for guest to report progress, and synchronize features. */
    /* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
    VIRTIO_CONFIG_S_ACKNOWLEDGE = 1,
    /* We have found a driver for the device. */
    VIRTIO_CONFIG_S_DRIVER = 2,
    /* Driver has used its parts of the config, and is happy */
    VIRTIO_CONFIG_S_DRIVER_OK = 4,
    /* We've given up on this device. */
    VIRTIO_CONFIG_S_FAILED = 0x80,
    /* Some virtio feature bits (currently bits 28 through 31) are reserved for the
     * transport being used (eg. virtio_ring), the rest are per-device feature
     * bits. */
    VIRTIO_TRANSPORT_F_START = 28,
    VIRTIO_TRANSPORT_F_END = 32,
    /* We support indirect buffer descriptors */
    VIRTIO_RING_F_INDIRECT_DESC = 28,
    /* The Guest publishes the used index for which it expects an interrupt
     * at the end of the avail ring. Host should ignore the avail->flags field. */
    /* The Host publishes the avail index for which it expects a kick
     * at the end of the used ring. Guest should ignore the used->flags field. */
    VIRTIO_RING_F_EVENT_IDX = 29,

    /* Do we get callbacks when the ring is completely used, even if we've
     * suppressed them? */
    VIRTIO_F_NOTIFY_ON_EMPTY = 24,
    /* A 32-bit r/o bitmask of the features supported by the host */
    VIRTIO_PCI_HOST_FEATURES = 0,
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
    VIRTIO_PCI_ABI_VERSION = 0,
    /* How many bits to shift physical queue address written to QUEUE_PFN.
     * 12 is historical, and due to x86 page size. */
    VIRTIO_PCI_QUEUE_ADDR_SHIFT = 12,
    /* The alignment to use between consumer and producer parts of vring.
     * x86 pagesize again. */
    VIRTIO_PCI_VRING_ALIGN = 4096,

};

enum {
    VIRTIO_VENDOR_ID = 0x1af4,
    VIRTIO_PCI_ID_MIN = 0x1000,
    VIRTIO_PCI_ID_MAX = 0x103f,

    VIRTIO_ID_NET     = 1,
    VIRTIO_ID_BLOCK   = 2,
    VIRTIO_ID_CONSOLE = 3,
    VIRTIO_ID_RNG     = 4,
    VIRTIO_ID_BALLOON = 5,
    VIRTIO_ID_RPMSG   = 7,
    VIRTIO_ID_SCSI    = 8,
    VIRTIO_ID_9P      = 9,
    VIRTIO_ID_RPROC_SERIAL = 11,
};

#define VIRTIO_ALIGN(x) ((x + (VIRTIO_PCI_VRING_ALIGN-1)) & ~(VIRTIO_PCI_VRING_ALIGN-1))

const unsigned max_virtqueues_nr = 64;

class virtio_driver : public hw_driver {
public:
    explicit virtio_driver(pci::device& dev);
    virtual ~virtio_driver();

    virtual const std::string get_name(void) = 0;

    virtual void dump_config(void);

    // The remaining space is defined by each driver as the per-driver
    // configuration space
    int virtio_pci_config_offset() {return (_dev.is_msix_enabled())? 24 : 20;}

    bool parse_pci_config(void);

    bool probe_virt_queues(void);
    vring* get_virt_queue(unsigned idx);

    // guest/host features physical access
    u32 get_device_features(void);
    bool get_device_feature_bit(int bit);
    void set_guest_features(u32 features);
    void set_guest_feature_bit(int bit, bool on);
    u32 get_guest_features(void);
    bool get_guest_feature_bit(int bit);

    // device status
    u32 get_dev_status(void);
    void set_dev_status(u32 status);
    void add_dev_status(u32 status);
    void del_dev_status(u32 status);

    // Access the virtio conf address space set by pci bar 1
    bool get_virtio_config_bit(u32 offset, int bit);
    void set_virtio_config_bit(u32 offset, int bit, bool on);

    // Access virtio config space
    void virtio_conf_read(u32 offset, void* buf, int length);
    void virtio_conf_write(u32 offset, void* buf, int length);
    u8 virtio_conf_readb(u32 offset) { return _bar1->readb(offset);};
    u16 virtio_conf_readw(u32 offset) { return _bar1->readw(offset);};
    u32 virtio_conf_readl(u32 offset) { return _bar1->read(offset);};
    void virtio_conf_writeb(u32 offset, u8 val) { _bar1->write(offset, val);};
    void virtio_conf_writew(u32 offset, u16 val) { _bar1->write(offset, val);};
    void virtio_conf_writel(u32 offset, u32 val) { _bar1->write(offset, val);};

    bool kick(int queue);
    void reset_host_side();
    void free_queues(void);

    bool get_indirect_buf_cap() {return _cap_indirect_buf;}
    void set_indirect_buf_cap(bool on) {_cap_indirect_buf = on;}

    pci::device& pci_device() { return _dev; }
protected:
    // Actual drivers should implement this on top of the basic ring features
    virtual u32 get_driver_features(void) { return (1 << VIRTIO_RING_F_INDIRECT_DESC); }
    bool setup_features(void);
protected:
    pci::device& _dev;
    interrupt_manager _msi;
    vring* _queues[max_virtqueues_nr];
    u32 _num_queues;
    pci::bar *_bar1;
    bool _cap_indirect_buf;
};

}

#endif

