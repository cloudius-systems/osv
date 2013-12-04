/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/random.hh"

#include <osv/device.h>

namespace randomdev {

struct random_device_priv {
    random_device* drv;
};

static random_device_priv *to_priv(device *dev)
{
    return reinterpret_cast<random_device_priv*>(dev->private_data);
}

static int
random_read(struct device *dev, struct uio *uio, int ioflags)
{
    auto prv = to_priv(dev);
    for (auto i = 0; i < uio->uio_iovcnt; i++) {
        auto *iov = &uio->uio_iov[i];
        auto nr = prv->drv->get_random_bytes(static_cast<char*>(iov->iov_base), iov->iov_len);

        uio->uio_resid  -= nr;
        uio->uio_offset += nr;

        if (nr < iov->iov_len) {
            break;
        }
    }

    return 0;
}

static struct devops random_device_devops {
    no_open,
    no_close,
    random_read,
    no_write,
    no_ioctl,
    no_devctl,
};

struct driver random_device_driver = {
    "random",
    &random_device_devops,
    sizeof(struct random_device_priv),
};

random_device::random_device()
{
    struct random_device_priv *prv;

    _random_dev = device_create(&random_device_driver, "random", D_CHR);
    prv = to_priv(_random_dev);
    prv->drv = this;
}

random_device::~random_device()
{
    device_destroy(_random_dev);
}

static hw_rng* s_hwrng;

void random_device::register_source(hw_rng* hwrng)
{
    s_hwrng = hwrng;
}

size_t random_device::get_random_bytes(char *buf, size_t size)
{
    if (s_hwrng) {
        return s_hwrng->get_random_bytes(buf, size);
    }
    return 0;
}

void randomdev_init()
{
    if (s_hwrng) {
        new random_device();
    }
}

}
