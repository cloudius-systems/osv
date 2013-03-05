#ifndef VIRTIO_DRIVER_H
#define VIRTIO_DRIVER_H

#include "driver.hh"
#include "virtio-device.hh"
#include "interrupt.hh"

namespace virtio {

class virtio_driver : public hw_driver {
public:
    explicit virtio_driver(virtio_device* vdev);
    virtual ~virtio_driver();

    virtual const std::string get_name(void) = 0;

    virtual void dump_config(void);

protected:
    // Actual drivers should implement this on top of the basic ring features
    virtual u32 get_driver_features(void) { return (1 << VIRTIO_RING_F_INDIRECT_DESC); }
    bool setup_features(void);

    ///////////////////
    // Device access //
    ///////////////////

    // Virtio device
    virtio_device *_dev;
    interrupt_manager _msi;
};

}

#endif

