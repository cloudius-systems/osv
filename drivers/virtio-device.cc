#include "drivers/pci-device.hh"
#include "drivers/virtio-vring.hh"

#include "virtio-device.hh"
#include "virtio-vring.hh"

using namespace pci;

namespace virtio {

    virtio_device::virtio_device(u8 bus, u8 device, u8 func)
        : pci_device(bus, device, func),  _num_queues(0), _bar1(nullptr)
    {
        for (unsigned i=0; i < max_virtqueues_nr; i++) {
            _queues[i] = nullptr;
        }
    }

    virtio_device::~virtio_device()
    {

    }

    void virtio_device::dump_config(void)
    {
        pci_device::dump_config();

        debug(fmt("    virtio features: "), false);
        for (int i=0;i<32;i++)
            debug(fmt("%d") % get_device_feature_bit(i), false);
        debug(fmt("\n"), false);
    }

    bool virtio_device::parse_pci_config(void)
    {
        if (!pci_device::parse_pci_config()) {
            return (false);
        }

        // Test whether bar1 is present
        _bar1 = get_bar(1);
        if (_bar1 == nullptr) {
            return (false);
        }

        // Check ABI version
        u8 rev = get_revision_id();
        if (rev != VIRTIO_PCI_ABI_VERSION) {
            debug(fmt("Wrong virtio revision=%x") % rev);
            return (false);
        }

        // Check device ID
        u16 dev_id = get_device_id();
        if ((dev_id < VIRTIO_PCI_ID_MIN) || (dev_id > VIRTIO_PCI_ID_MAX)) {
            debug(fmt("Wrong virtio dev id %x") % dev_id);
            return (false);
        }

        return (true);
    }

    void virtio_device::reset_host_side()
    {
        set_dev_status(0);
        virtio_conf_writel(VIRTIO_PCI_QUEUE_PFN,(u32)0);
    }

    void virtio_device::free_queues(void)
    {
        for (unsigned i=0; i < max_virtqueues_nr; i++) {
            if (nullptr != _queues[i]) {
                delete (_queues[i]);
                _queues[i] = nullptr;
            }
        }
    }

    bool virtio_device::kick(int queue)
    {
        virtio_conf_writel(VIRTIO_PCI_QUEUE_NOTIFY, queue);
        return true;
    }

    bool virtio_device::probe_virt_queues(void)
    {
        u16 qsize = 0;

        do {

            if (_num_queues >= max_virtqueues_nr) {
                return false;
            }

            // Read queue size
            virtio_conf_writel(VIRTIO_PCI_QUEUE_SEL, _num_queues);
            qsize = virtio_conf_readw(VIRTIO_PCI_QUEUE_NUM);
            if (0 == qsize) {
                break;
            }

            // Init a new queue
            vring * queue = new vring(this, qsize, _num_queues);
            _queues[_num_queues++] = queue;

            // Tell host about pfn
            // TODO: Yak, this is a bug in the design, on large memory we'll have PFNs > 32 bit
            // Dor to notify Rusty
            virtio_conf_writel(VIRTIO_PCI_QUEUE_PFN, (u32)(queue->get_paddr() >> VIRTIO_PCI_QUEUE_ADDR_SHIFT));

            // Debug print
            debug(fmt("Queue[%d] -> size %d, paddr %x") % (_num_queues-1) % qsize % queue->get_paddr());

        } while (true);

        return true;
    }

    vring* virtio_device::get_virt_queue(unsigned idx)
    {
        if (idx >= _num_queues) {
            return (nullptr);
        }

        return (_queues[idx]);
    }

    u32 virtio_device::get_device_features(void)
    {
        return (virtio_conf_readl(VIRTIO_PCI_HOST_FEATURES));
    }

    bool virtio_device::get_device_feature_bit(int bit)
    {
        return (get_virtio_config_bit(VIRTIO_PCI_HOST_FEATURES, bit));
    }

    void virtio_device::set_guest_features(u32 features)
    {
        virtio_conf_writel(VIRTIO_PCI_GUEST_FEATURES, features);
    }

    void virtio_device::set_guest_feature_bit(int bit, bool on)
    {
        set_virtio_config_bit(VIRTIO_PCI_GUEST_FEATURES, bit, on);
    }

    u32 virtio_device::get_dev_status(void)
    {
        return (virtio_conf_readl(VIRTIO_PCI_STATUS));
    }

    void virtio_device::set_dev_status(u32 status)
    {
        virtio_conf_writel(VIRTIO_PCI_STATUS, status);
    }

    void virtio_device::add_dev_status(u32 status)
    {
        set_dev_status(get_dev_status() | status);
    }

    void virtio_device::del_dev_status(u32 status)
    {
        set_dev_status(get_dev_status() & ~status);
    }

    bool virtio_device::get_virtio_config_bit(u32 offset, int bit)
    {
        return (virtio_conf_readl(offset) & (1 << bit));
    }

    void virtio_device::set_virtio_config_bit(u32 offset, int bit, bool on)
    {
        u32 val = virtio_conf_readl(offset);
        u32 newval = ( val & ~(1 << bit) ) | ((int)(on)<<bit);
        virtio_conf_writel(offset, newval);
    }

    void virtio_device::virtio_conf_write(u32 offset, void* buf, int length)
    {
        u8* ptr = reinterpret_cast<u8*>(buf);
        for (int i=0;i<length;i++)
            _bar1->write(offset+i, ptr[i]);
    }

    void virtio_device::virtio_conf_read(u32 offset, void* buf, int length)
    {
        unsigned char* ptr = reinterpret_cast<unsigned char*>(buf);
        for (int i=0;i<length;i++)
            ptr[i] = _bar1->readb(offset+i);
    }
}
