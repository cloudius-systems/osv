/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef RANDOM_DEVICE_H
#define RANDOM_DEVICE_H

#include <osv/device.h>
#include <osv/types.h>
#include <memory>

namespace randomdev {

class hw_rng;

class random_device {
public:

    random_device();
    virtual ~random_device();

    static void register_source(hw_rng* hwrng);

private:

    device* _random_dev;
    device* _urandom_dev;
};

class hw_rng {
public:
    virtual size_t get_random_bytes(char *buf, size_t size) = 0;
};

void randomdev_init();

}

#endif
