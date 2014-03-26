/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ZFS_DEVICE_H
#define ZFS_DEVICE_H

#include <osv/device.h>
#include <osv/types.h>

namespace zfsdev {

class zfs_device {
public:

    zfs_device();
    virtual ~zfs_device();

private:

    device* _zfs_dev;
};

void zfsdev_init();

}

#endif
