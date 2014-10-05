/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This is the Linux-specific asynchronous I/O API / ABI from libaio.
// Note that this API is different the Posix AIO API.

#ifndef INCLUDED_LIBAIO_H
#define INCLUDED_LIBAIO_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct io_context *io_context_t;

int io_setup(int nr_events, io_context_t *ctxp_idp);

#ifdef __cplusplus
}
#endif

#endif
