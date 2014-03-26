/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/zfs.hh"

#include <osv/device.h>

namespace zfsdev {

extern "C" int osv_zfs_ioctl(unsigned long req, void* buffer);

struct zfs_device_priv {
    zfs_device* drv;
};

static zfs_device_priv *to_priv(device *dev)
{
    return reinterpret_cast<zfs_device_priv*>(dev->private_data);
}

static int zfs_ioctl(device* dev, ulong req, void* buffer)
{
    return osv_zfs_ioctl(req, buffer);
}

static devops zfs_device_devops = {
    no_open,
    no_close,
    no_read,
    no_write,
    zfs_ioctl,
    no_devctl,
};

struct driver zfs_device_driver = {
    "zfs",
    &zfs_device_devops,
    sizeof(struct zfs_device_priv),
};

zfs_device::zfs_device()
{
    struct zfs_device_priv *prv;

    zfs_device_driver.flags = 0;
    _zfs_dev = device_create(&zfs_device_driver, "zfs", D_CHR);
    prv = to_priv(_zfs_dev);
    prv->drv = this;
}

zfs_device::~zfs_device()
{
    device_destroy(_zfs_dev);
}

void zfsdev_init(void)
{
    new zfs_device();
}

}
