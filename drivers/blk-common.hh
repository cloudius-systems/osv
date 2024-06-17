/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef BLK_COMMON_HH
#define BLK_COMMON_HH

#include <osv/device.h>

int blk_ioctl(struct device* dev, u_long io_cmd, void* buf);

#endif
