#include "drivers/virtio.hh"
#include "drivers/virtio-net.hh"
#include "drivers/pci-device.hh"

#include "debug.hh"


namespace virtio {


    virtio_net::virtio_net()
        : virtio_driver(VIRTIO_NET_DEVICE_ID)
    {

    }

    virtio_net::~virtio_net()
    {
    }

    bool virtio_net::load(void)
    {
        virtio_driver::load();
        
        add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

        return true;
    }

    bool virtio_net::unload(void)
    {
        return (true);
    }

}

