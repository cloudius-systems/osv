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

#include <string>

int blk_ioctl(struct device* dev, u_long io_cmd, void* buf);

// Allocate the next free name in the shared "vblk<N>" block device namespace.
//
// Every block driver registers its disks in the same flat devfs namespace, and
// device_register() treats a name collision as fatal (sys_panic("duplicate
// device")).  A per-driver counter is therefore not sufficient: two different
// drivers each starting at 0 both produce "vblk0", and the second one to probe
// panics the kernel.  Drawing every name from this single allocator keeps them
// unique no matter which drivers are compiled in or what order they probe in.
std::string blk_next_device_name();

#endif
