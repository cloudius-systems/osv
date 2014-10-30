/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_ASSIGNED_DRIVER_H
#define VIRTIO_ASSIGNED_DRIVER_H

#include <drivers/driver.hh>

namespace virtio {
namespace assigned {
hw_driver* probe_net(hw_device* dev);
}
}

#endif

