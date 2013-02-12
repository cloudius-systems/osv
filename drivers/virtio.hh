#ifndef VIRTIO_DRIVER_H
#define VIRTIO_DRIVER_H

#include "driver.hh"
#include "virtio-device.hh"

namespace virtio {

    class virtio_driver : public hw_driver {
    public:    
        virtio_driver(u16 device_id, unsigned dev_idx=0);
        virtual ~virtio_driver();

        virtual const std::string get_name(void) = 0;

        // hw_driver interface
        virtual bool hw_probe(void);
        virtual bool load(void);
        virtual bool unload(void) = 0;
        virtual void dump_config(void);

    protected:
        u16 _device_id;

        // Actual drivers should implement this on top of the basic ring features
        virtual u32 get_driver_features(void) { return (1 << VIRTIO_RING_F_INDIRECT_DESC); }
        bool setup_features(void);

        ///////////////////
        // Device access //
        ///////////////////

        // Virtio device
        virtio_device *_dev;
        // The idx of the device, if there are more than one
        // of the same type
        unsigned _dev_idx;
    };

}

#endif

