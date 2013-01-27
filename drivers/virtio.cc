#include <string.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-device.hh"

#include "debug.hh"

using namespace pci;

namespace virtio {

    virtio_driver::virtio_driver(u16 device_id)
        : hw_driver(),
          _device_id(device_id), _dev(nullptr)
    {

    }

    virtio_driver::~virtio_driver()
    {
        _dev->reset_host_side();
        _dev->free_queues();
    }

    bool virtio_driver::early_init_checks()
    {
        u8 rev = _dev->get_revision_id();
        if (rev != VIRTIO_PCI_ABI_VERSION) {
            debug(fmt("Wrong virtio revision=%x") % rev);
            return false;
        }

        u16 dev_id = _dev->get_device_id();

        if (dev_id < VIRTIO_PCI_ID_MIN || dev_id > VIRTIO_PCI_ID_MAX) {
            debug(fmt("Wrong virtio dev id %x") % _dev->get_device_id());

            return false;
        }

        // debug(fmt("%s passed. Subsystem: vid:%x:id:%x") % __FUNCTION__ % (u16)_dev->get_subsystem_vid() % (u16)_dev->get_subsystem_id());
        return true;
    }


    bool virtio_driver::hw_probe(void)
    {
        _dev = dynamic_cast<virtio_device *>(device_manager::instance()->
            get_device(hw_device_id(VIRTIO_VENDOR_ID, _device_id)));
        if (_dev == nullptr) {
            return (false);
        }

        return (early_init_checks());
    }

    bool virtio_driver::load(void)
    {
        _dev->set_bus_master(true);

        //make sure the queue is reset
        _dev->reset_host_side();

        // Acknowledge device
        _dev->add_dev_status(VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER);

        // Generic init of virtqueues
        _dev->probe_virt_queues();

        setup_features();

        return true;
    }

    bool virtio_driver::unload(void)
    {
        // TODO: implement
        return (true);
    }

    bool virtio_driver::setup_features(void)
    {
        u32 dev_features = _dev->get_device_features();
        u32 drv_features = this->get_driver_features();

        u32 subset = dev_features & drv_features;

        // Configure transport features
        // TBD
        return (subset == 1);

    }

    void virtio_driver::dump_config()
    {
        debug(fmt("virtio_driver vid:id= %x:%x") % _dev->get_vendor_id() % _dev->get_device_id());
    }



}

