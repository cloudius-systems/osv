#include "drivers/virtio.hh"
#include "drivers/virtio-net.hh"

#include "debug.hh"


namespace virtio {


    virtio_net::virtio_net()
        : virtio_driver(VIRTIO_NET_DEVICE_ID)
    {

    }

    virtio_net::~virtio_net()
    {
        set_dev_status(0);
    }

    bool virtio_net::Init(Device *d)
    {
        virtio_driver::Init(d);
        
        add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

        return true;
    }

}

