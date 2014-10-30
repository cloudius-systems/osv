/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_ASSIGNED_API_H
#define VIRTIO_ASSIGNED_API_H

#include <functional>
#include <stdint.h>

namespace osv {

class assigned_virtio {
public:
    // API for getting one assigned virtio device. This function is marked
    // "weak" so an application can check for its existance (it won't exist,
    // of course, on Linux).
    // TODO: provide a way to get one of multiple assigned virtio devices.
    static assigned_virtio *get() __attribute__((weak));

    virtual void kick(int queue) = 0;
    virtual uint32_t queue_size(int queue) = 0;
    virtual void enable_interrupt(unsigned int queue,
            std::function<void(void)> handler) = 0;
    virtual void set_queue_pfn(int queue, uint64_t phys) = 0;
    virtual uint32_t init_features(uint32_t driver_features) = 0;
    virtual void set_driver_ok() = 0;
    virtual void conf_read(void *buf, int length) = 0;

    virtual ~assigned_virtio();

    // virt_to_phys is static, as it does not depend on which device was
    // assigned. We can even make it inline for a bit of extra speed.
    static uint64_t virt_to_phys(void* p) __attribute__((weak));
};

}

#endif

