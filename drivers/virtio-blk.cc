#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"

#include "debug.hh"


namespace virtio {


    virtio_blk::virtio_blk()
        : virtio_driver(VIRTIO_BLK_DEVICE_ID)
    {

    }

    virtio_blk::~virtio_blk()
    {

    }

    bool virtio_blk::Init(Device *d)
    {
        virtio_driver::Init(d);
        
        add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

        return true;
    }

}

