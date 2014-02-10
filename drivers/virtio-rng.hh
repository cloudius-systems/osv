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
#include "drivers/random.hh"

#include <vector>

namespace virtio {

class rng : public virtio_driver, randomdev::hw_rng {
public:
    enum {
        VIRTIO_RNG_DEVICE_ID = 0x1005,
    };

    explicit rng(pci::device& dev);
    virtual ~rng();

    virtual const std::string get_name() { return "virtio-rng"; }

    virtual size_t get_random_bytes(char *buf, size_t size) override;

    static hw_driver* probe(hw_device* dev);

private:

    void handle_irq();
    bool ack_irq();
    void worker();
    void refill();

    static const size_t _pool_size = 64;
    std::vector<char> _entropy;
    gsi_level_interrupt _gsi;
    sched::thread _thread;
    condvar _producer;
    condvar _consumer;
    vring* _queue;
    mutex _mtx;
};

}

#endif
