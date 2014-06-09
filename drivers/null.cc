/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/device.h>
#include <osv/uio.h>

namespace nulldev {

static int
null_read(struct device *dev, struct uio *uio, int ioflags)
{
    return 0;
}

static int
null_write(struct device *dev, struct uio *uio, int ioflags)
{
    uio->uio_resid = 0;
    return 0;
}

static struct devops null_device_devops {
    no_open,
    no_close,
    null_read,
    null_write,
    no_ioctl,
    no_devctl,
};

struct driver null_device_driver = {
    "null",
    &null_device_devops,
};

void nulldev_init()
{
    device_create(&null_device_driver, "null", D_CHR);
}

}
