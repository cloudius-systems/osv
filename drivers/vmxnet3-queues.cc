/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <string.h>
#include <osv/mempool.hh>
#include <osv/mmu.hh>

#include "virtio.hh"
#include "drivers/vmxnet3-queues.hh"
#include <osv/debug.hh>

namespace vmw {

#define vmxnet3_tag "vmxnet3"
#define vmxnet3_d(...)   tprintf_d(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_i(...)   tprintf_i(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_w(...)   tprintf_w(vmxnet3_tag, __VA_ARGS__)
#define vmxnet3_e(...)   tprintf_e(vmxnet3_tag, __VA_ARGS__)

void vmxnet3_drv_shared::attach(void *storage)
{
    vmxnet3_layout_holder<vmxnet3_shared_layout>::attach(storage);

    layout->magic = VMXNET3_REV1_MAGIC;

    // DriverInfo
    layout->version = VMXNET3_DRIVER_VERSION;
    layout->guest = VMXNET3_GOS_FREEBSD | VMXNET3_GUEST_OS_VERSION |
        (sizeof(void*) == sizeof(u32) ? VMXNET3_GOS_32BIT : VMXNET3_GOS_64BIT);

    layout->vmxnet3_revision = VMXNET3_REVISION;
    layout->upt_version = VMXNET3_UPT_VERSION;
}

};
