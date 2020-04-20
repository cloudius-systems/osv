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

struct fuse_request {
    struct fuse_in_header in_header;
    struct fuse_out_header out_header;

    void* input_args_data;
    size_t input_args_size;

    void* output_args_data;
    size_t output_args_size;

    mutex_t req_mutex;
    waitqueue req_wait;
};

struct fuse_strategy {
    void* drv;
    int (*make_request)(void*, fuse_request*);
};

int fuse_req_send_and_receive_reply(fuse_strategy* strategy, uint32_t opcode,
    uint64_t nodeid, void* input_args_data, size_t input_args_size,
    void* output_args_data, size_t output_args_size);

void fuse_req_wait(fuse_request* req);

#endif
