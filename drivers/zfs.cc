/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/zfs.hh"

#include <osv/device.h>
#include <osv/export.h>

namespace zfsdev {

//The osv_zfs_ioctl_fun will be set dynamically in INIT function of
//libsolaris.so by calling register_osv_zfs_ioctl() below. The osv_zfs_ioctl()
//is a function defined in libsolaris.so.
int (*osv_zfs_ioctl_fun)(unsigned long req, void* buffer);

struct zfs_device_priv {
    zfs_device* drv;
};

static zfs_device_priv *to_priv(device *dev)
{
    return reinterpret_cast<zfs_device_priv*>(dev->private_data);
}

static int zfs_ioctl(device* dev, ulong req, void* buffer)
{
    return (*osv_zfs_ioctl_fun)(req, buffer);
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

//Needs to be a C-style function so it can be called from libsolaris.so
extern "C" OSV_LIBSOLARIS_API void register_osv_zfs_ioctl( int (*osv_zfs_ioctl_fun)(unsigned long, void*)) {
    zfsdev::osv_zfs_ioctl_fun = osv_zfs_ioctl_fun;
}
