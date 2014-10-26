/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This is the Linux-specific asynchronous I/O API / ABI from libaio.
// Note that this API is different the Posix AIO API.

#include <api/libaio.h>

#include <osv/stubbing.hh>

int io_setup(int nr_events, io_context_t *ctxp_idp) {
    // This is a stub that doesn't actually do anything. If the caller tries
    // to follow the io_setup() call with any other libaio call, those will
    // fail.
    WARN_STUBBED();
    if (nr_events < 0) {
        return -EINVAL;
    }
    return 0;
}

UNIMPL(int io_submit(io_context_t ctx, long nr, struct iocb *ios[]))
UNIMPL(int io_getevents(io_context_t ctx_id, long min_nr, long nr,
        struct io_event *events, struct timespec *timeout))
UNIMPL(int io_destroy(io_context_t ctx))
UNIMPL(int io_cancel(io_context_t ctx, struct iocb *iocb, struct io_event *evt))
