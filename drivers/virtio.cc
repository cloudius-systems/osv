#include <string.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-device.hh"

#include "debug.hh"

using namespace pci;

namespace virtio {

    virtio_driver::virtio_driver(virtio_device* vdev)
        : hw_driver()
        , _dev(vdev)
        , _msi(&vdev->pci_device())
    {
        _dev->pci_device().set_bus_master(true);

        _dev->pci_device().msix_enable();

        //make sure the queue is reset
        _dev->reset_host_side();

        // Acknowledge device
        _dev->add_dev_status(VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

        // Generic init of virtqueues
        _dev->probe_virt_queues();

        setup_features();
    }

    virtio_driver::~virtio_driver()
    {
        _dev->reset_host_side();
        _dev->free_queues();
    }

    bool virtio_driver::setup_features(void)
    {
        u32 dev_features = _dev->get_device_features();
        u32 drv_features = this->get_driver_features();

        u32 subset = dev_features & drv_features;

        //notify the host about the features in used according
        //to the virtio spec
        for (int i=0;i<32;i++)
            if (subset & (1 << i))
                virtio_d(fmt("%s: found feature intersec of bit %d") % __FUNCTION__ % i);

        if (subset & (1 << VIRTIO_RING_F_INDIRECT_DESC))
            _dev->set_indirect_buf_cap(true);

        _dev->set_guest_features(subset);

        return (subset != 0);

    }

    void virtio_driver::dump_config()
    {
        u8 B, D, F;
        _dev->pci_device().get_bdf(B, D, F);

        virtio_d(fmt("%s [%x:%x.%x] vid:id= %x:%x") % get_name() %
            (u16)B % (u16)D % (u16)F %
            _dev->pci_device().get_vendor_id() %
            _dev->pci_device().get_device_id());
    }



}

