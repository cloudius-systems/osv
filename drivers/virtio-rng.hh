/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_RNG_DRIVER_H
#define VIRTIO_RNG_DRIVER_H

#include <osv/condvar.h>
#include <osv/device.h>
#include <osv/mutex.h>

#include "drivers/virtio.hh"
#include "drivers/device.hh"

#include <valarray>

namespace virtio {

class virtio_rng : public virtio_driver {
public:
    enum {
        VIRTIO_RNG_DEVICE_ID = 0x1005,
    };

    explicit virtio_rng(pci::device& dev);
    virtual ~virtio_rng();

    virtual const std::string get_name(void) { return "virtio-rng"; }

    u32 get_random_bytes(char *buf, u32 size);

    static hw_driver* probe(hw_device* dev);

private:

    void handle_irq();
    void ack_irq();
    void worker();
    void refill();

    std::valarray<char> _entropy;
    gsi_level_interrupt _gsi;
    sched::thread _thread;
    device* _urandom_dev;
    device* _random_dev;
    u32 _entropy_count;
    condvar _producer;
    condvar _consumer;
    vring* _queue;
    mutex _mtx;
};

}

#endif
