#ifndef VIRTIO_DRIVER_H
#define VIRTIO_DRIVER_H

#include "arch/x64/processor.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"

class Virtio : public Driver {
public:
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

    /* The remaining space is defined by each driver as the per-driver
     * configuration space */
    #define VIRTIO_PCI_CONFIG(dev)      ((dev)->msix_enabled ? 24 : 20)

    enum VIRTIO_VRING {
        /* This marks a buffer as continuing via the next field. */
        VRING_DESC_F_NEXT = 1,
        /* This marks a buffer as write-only (otherwise read-only). */
        VRING_DESC_F_WRITE = 2,
        /* This means the buffer contains a list of buffer descriptors. */
        VRING_DESC_F_INDIRECT = 4,
        /* The Host uses this in used->flags to advise the Guest: don't kick me when
         * you add a buffer.  It's unreliable, so it's simply an optimization.  Guest
         * will still kick if it's out of buffers. */
        VRING_USED_F_NO_NOTIFY = 1,
        /* The Guest uses this in avail->flags to advise the Host: don't interrupt me
         * when you consume a buffer.  It's unreliable, so it's simply an
         * optimization.  */
        VRING_AVAIL_F_NO_INTERRUPT = 1,

        /* We support indirect buffer descriptors */
        VIRTIO_RING_F_INDIRECT_DESC = 28,

        /* The Guest publishes the used index for which it expects an interrupt
         * at the end of the avail ring. Host should ignore the avail->flags field. */
        /* The Host publishes the avail index for which it expects a kick
         * at the end of the used ring. Guest should ignore the used->flags field. */
        VIRTIO_RING_F_EVENT_IDX = 29,
    };

    /* Virtio ring descriptors: 16 bytes.  These can chain together via "next". */
    struct vring_desc {
        /* Address (guest-physical). */
        u64 addr;
        /* Length. */
        u32 len;
        /* The flags as indicated above. */
        u16 flags;
        /* We chain unused descriptors via this, too */
        u16 next;
    };

    struct vring_avail {
        u16 flags;
        u16 idx;
        u16 ring[];
    };

    /* u32 is used here for ids for padding reasons. */
    struct vring_used_elem {
        /* Index of start of used descriptor chain. */
        u32 id;
        /* Total length of the descriptor chain which was used (written to) */
        u32 len;
    };

    struct vring_used {
        u16 flags;
        u16 idx;
        struct vring_used_elem ring[];
    };

    struct vring {
        unsigned int num;

        struct vring_desc *desc;

        struct vring_avail *avail;

        struct vring_used *used;
    };

    /* The standard layout for the ring is a continuous chunk of memory which looks
     * like this.  We assume num is a power of 2.
     *
     * struct vring
     * {
     *  // The actual descriptors (16 bytes each)
     *  struct vring_desc desc[num];
     *
     *  // A ring of available descriptor heads with free-running index.
     *  __u16 avail_flags;
     *  __u16 avail_idx;
     *  __u16 available[num];
     *  __u16 used_event_idx;
     *
     *  // Padding to the next align boundary.
     *  char pad[];
     *
     *  // A ring of used descriptor heads with free-running index.
     *  __u16 used_flags;
     *  __u16 used_idx;
     *  struct vring_used_elem used[num];
     *  __u16 avail_event_idx;
     * };
     */
    /* We publish the used event index at the end of the available ring, and vice
     * versa. They are at the end for backwards compatibility. */
    #define vring_used_event(vr) ((vr)->avail->ring[(vr)->num])
    #define vring_avail_event(vr) (*(u16 *)&(vr)->used->ring[(vr)->num])

    Virtio(u16 id) : Driver(VIRTIO_VENDOR_ID, id) {};
    virtual void dumpConfig() const;
    virtual bool Init(Device *d);

protected:

    virtual bool earlyInitChecks();
    void vring_init(struct vring *vr, unsigned int num, void *p,
                    unsigned long align);
    unsigned vring_size(unsigned int num, unsigned long align);
    int vring_need_event(u16 event_idx, u16 new_idx, u16 old);

    virtual bool get_device_feature_bit(int bit);
    virtual void set_guest_feature_bit(int bit, bool on);
    virtual void set_guest_features(u32 features);
    void pci_conf_read(int offset, void* buf, int length);
    void pci_conf_write(int offset, void* buf, int length);
    u8 pci_conf_readb(int offset) {return _bars[0]->readb(offset);};
    u16 pci_conf_readw(int offset) {return _bars[0]->readw(offset);};
    u32 pci_conf_readl(int offset) {return _bars[0]->read(offset);};
    void pci_conf_write(int offset, u8 val) {_bars[0]->write(offset, val);};
    void pci_conf_write(int offset, u16 val) {_bars[0]->write(offset, val);};
    void pci_conf_write(int offset, u32 val) {_bars[0]->write(offset, val);};

    void probe_virt_queues();

private:
};

#endif
