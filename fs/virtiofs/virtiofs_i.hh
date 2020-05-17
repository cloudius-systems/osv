/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIOFS_IO_H
#define VIRTIOFS_IO_H

#include "fuse_kernel.h"
#include <osv/mutex.h>
#include <osv/waitqueue.hh>
#include "drivers/virtio-fs.hh"

int fuse_req_send_and_receive_reply(virtio::fs* drv, uint32_t opcode,
    uint64_t nodeid, void* input_args_data, size_t input_args_size,
    void* output_args_data, size_t output_args_size);

#endif
