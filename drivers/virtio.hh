#ifndef VIRTIO_DRIVER_H
#define VIRTIO_DRIVER_H

#include <list>
#include <string>

#include "arch/x64/processor.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"

#include "drivers/virtio-vring.hh"

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

    #define VIRTIO_ALIGN(x) ((x + (VIRTIO_PCI_VRING_ALIGN-1)) & ~(VIRTIO_PCI_VRING_ALIGN-1))

    const int max_virtqueues_nr = 64;

    class virtio_driver : public Driver {
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

        // The remaining space is defined by each driver as the per-driver
        // configuration space
        // TODO 'have' doesn't means it is enabled, needs fixing (24 when enabled)
        #define VIRTIO_PCI_CONFIG(drv)      ((drv)->_have_msix ? 20 : 20)

        virtio_driver(u16 device_id);
        virtual ~virtio_driver();
                
        virtual bool Init(Device *d);
        virtual void dumpConfig() const;

        bool kick(int queue);
        void reset_host_side();

    protected:
        
        vring *_queues[max_virtqueues_nr];
        int num_queues;

        virtual bool earlyInitChecks(void);
        bool probe_virt_queues(void);    
        bool setup_features(void);

        // Actual drivers should implement this
        virtual u32 get_driver_features(void) { return (0); }      


        ///////////////////
        // Device access //
        ///////////////////

        // guest/host features physical access
        u32 get_device_features(void);
        bool get_device_feature_bit(int bit);
        void set_guest_features(u32 features);
        void set_guest_feature_bit(int bit, bool on);

        // device status
        u32 get_dev_status(void);
        void set_dev_status(u32 status);
        void add_dev_status(u32 status);
        void del_dev_status(u32 status);

        // access the virtio conf address space set by pci bar 0
        u32 get_virtio_config(int offset);
        void set_virtio_config(int offset, u32 val);
        bool get_virtio_config_bit(int offset, int bit);
        void set_virtio_config_bit(int offset, int bit, bool on);

        void pci_conf_read(int offset, void* buf, int length);
        void pci_conf_write(int offset, void* buf, int length);
        u8 pci_conf_readb(int offset) {return _bars[0]->readb(offset);};
        u16 pci_conf_readw(int offset) {return _bars[0]->readw(offset);};
        u32 pci_conf_readl(int offset) {return _bars[0]->read(offset);};
        void pci_conf_write(int offset, u8 val) {_bars[0]->write(offset, val);};
        void pci_conf_write(int offset, u16 val) {_bars[0]->write(offset, val);};
        void pci_conf_write(int offset, u32 val) {_bars[0]->write(offset, val);};

    private:
    };

}

#endif

