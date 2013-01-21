#include <string.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-vring.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-device.hh"

#include "debug.hh"

using namespace pci;

namespace virtio {

    virtio_driver::virtio_driver(u16 device_id)
        : Driver(VIRTIO_VENDOR_ID, device_id), _bar1(nullptr)
    {
        for (int i=0; i < max_virtqueues_nr; i++) {
            _queues[i] = NULL;
        }

        num_queues = 0;
    }

    virtio_driver::~virtio_driver()
    {
        reset_host_side();
        for (int i=0; i < max_virtqueues_nr; i++) {
            if (NULL != _queues[i]) {
                delete (_queues[i]);
            }
        }
    }

    void virtio_driver::reset_host_side() {
        set_dev_status(0);
        pci_conf_write(VIRTIO_PCI_QUEUE_PFN,(u32)0);
    }

    bool virtio_driver::earlyInitChecks()
    {
    	if (!Driver::earlyInitChecks()) {
    		return false;
    	}

        u8 rev;
        if (_dev->get_revision_id() != VIRTIO_PCI_ABI_VERSION) {
            debug(fmt("Wrong virtio revision=%x") % rev);
            return false;
        }

        u16 dev_id = _dev->get_device_id();

        if (dev_id < VIRTIO_PCI_ID_MIN || dev_id > VIRTIO_PCI_ID_MAX) {
            debug(fmt("Wrong virtio dev id %x") % _id);

            return false;
        }

        debug(fmt("%s passed. Subsystem: vid:%x:id:%x") % __FUNCTION__ % (u16)_dev->get_subsystem_vid() % (u16)_dev->get_subsystem_id());
        return true;
    }

    bool virtio_driver::Init(pci_device* dev)
    {
        if (!Driver::Init(dev)) {
            return (false);
        }

        _bar1 = _dev->get_bar(1);

        //make sure the queue is reset
        reset_host_side();

        // Acknowledge device
        add_dev_status(VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

        // Generic init of virtqueues
        probe_virt_queues();
        setup_features();
#if 0
        for (int i=0;i<32;i++)
            debug(fmt("%d:%d ") % i % get_device_feature_bit(i), false);
        debug(fmt("\n"), false);
#endif

        return true;
    }

    bool virtio_driver::kick(int queue)
    {
        set_virtio_config(VIRTIO_PCI_QUEUE_NOTIFY, queue);
        return true;
    }

    bool virtio_driver::probe_virt_queues(void) 
    {
        u16 queuesel = 0;
        u16 qsize = 0;
        
        do {

            if (queuesel >= max_virtqueues_nr) {
                return false;
            }

            // Read queue size        
            pci_conf_write(VIRTIO_PCI_QUEUE_SEL, queuesel);
            qsize = pci_conf_readw(VIRTIO_PCI_QUEUE_NUM);
            if (0 == qsize) {
                break;
            }

            // Init a new queue
            vring * queue = new vring(this, qsize, queuesel);
            _queues[queuesel++] = queue;

            // Tell host about pfn
            // TODO: Yak, this is a bug in the design, on large memory we'll have PFNs > 32 bit
            // Dor to notify Rusty
            pci_conf_write(VIRTIO_PCI_QUEUE_PFN, (u32)(queue->get_paddr() >> VIRTIO_PCI_QUEUE_ADDR_SHIFT));

            // Debug print
            debug(fmt("Queue[%d] -> size %d, paddr %x") % (queuesel-1) % qsize % queue->get_paddr());
            
        } while (true);

        return true;
    }


    bool virtio_driver::setup_features(void)
    {
        u32 dev_features = this->get_device_features();
        u32 drv_features = this->get_driver_features();

        u32 subset = dev_features & drv_features;

        // Configure transport features
        // TBD       
        return (subset == 1);

    }

    void virtio_driver::dump_config()
    {
        Driver::dump_config();
        debug(fmt("virtio_driver vid:id= %x:%x") % _vid % _id);
    }


    u32 virtio_driver::get_device_features(void)
    {
        return (get_virtio_config(VIRTIO_PCI_HOST_FEATURES));
    }

    bool virtio_driver::get_device_feature_bit(int bit)
    {
        return (get_virtio_config_bit(VIRTIO_PCI_HOST_FEATURES, bit));
    }

    void virtio_driver::set_guest_features(u32 features)
    {
        set_virtio_config(VIRTIO_PCI_GUEST_FEATURES, features);
    }

    void virtio_driver::set_guest_feature_bit(int bit, bool on)
    {
        set_virtio_config_bit(VIRTIO_PCI_GUEST_FEATURES, bit, on);
    }

    u32 virtio_driver::get_dev_status(void)
    {
        return (get_virtio_config(VIRTIO_PCI_STATUS));
    }

    void virtio_driver::set_dev_status(u32 status)
    {
        set_virtio_config(VIRTIO_PCI_STATUS, status);
    }

    void virtio_driver::add_dev_status(u32 status)
    {
        set_dev_status(get_dev_status() | status);
    }

    void virtio_driver::del_dev_status(u32 status)
    {
        set_dev_status(get_dev_status() & ~status);
    }

    u32 virtio_driver::get_virtio_config(int offset)
    {
        return (_bar1->read(offset));
    }

    void virtio_driver::set_virtio_config(int offset, u32 val)
    {
        _bar1->write(offset, val);
    }

    bool virtio_driver::get_virtio_config_bit(int offset, int bit)
    {
        return (get_virtio_config(offset) & (1 << bit));
    }

    void virtio_driver::set_virtio_config_bit(int offset, int bit, bool on)
    {
        u32 val = get_virtio_config(offset);
        u32 newval = ( val & ~(1 << bit) ) | ((int)(on)<<bit);
        set_virtio_config(offset, newval);
    }


    void virtio_driver::pci_conf_write(int offset, void* buf, int length)
    {
        u8* ptr = reinterpret_cast<u8*>(buf);
        for (int i=0;i<length;i++)
            _bar1->write(offset+i, ptr[i]);
    }

    void virtio_driver::pci_conf_read(int offset, void* buf, int length)
    {
        unsigned char* ptr = reinterpret_cast<unsigned char*>(buf);
        for (int i=0;i<length;i++)
            ptr[i] = _bar1->readb(offset+i);
    }

}

