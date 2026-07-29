/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This is the Linux-specific asynchronous I/O API / ABI from libaio.
// Note that this API is different from the POSIX AIO API.

#ifndef INCLUDED_LIBAIO_H
#define INCLUDED_LIBAIO_H

#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct io_context *io_context_t;

// Command opcodes, matching the Linux aio ABI (linux/aio_abi.h).
typedef enum io_iocb_cmd {
    IO_CMD_PREAD   = 0,
    IO_CMD_PWRITE  = 1,
    IO_CMD_FSYNC   = 2,
    IO_CMD_FDSYNC  = 3,
    IO_CMD_POLL    = 5,
    IO_CMD_NOOP    = 6,
    IO_CMD_PREADV  = 7,
    IO_CMD_PWRITEV = 8,
} io_iocb_cmd_t;

// read() from an aio context returns these; also filled by io_getevents().
struct io_event {
    uint64_t data;   // the aio_data field copied from the iocb
    uint64_t obj;    // pointer to the iocb this event came from
    int64_t  res;    // primary result (bytes transferred, or -errno)
    int64_t  res2;   // secondary result (unused, always 0 here)
};

// The iocb layout matches the Linux aio ABI (64 bytes on x86-64/aarch64).  We
// use the same field split as libaio's <libaio.h> so binaries built against it
// interoperate: the first union member exposes the common PREAD/PWRITE fields.
struct iocb {
    uint64_t aio_data;         // returned in io_event.data

    // aio_key/aio_rw_flags on little-endian.  We do not consume these.
    uint32_t aio_key;
    int32_t  aio_rw_flags;

    uint16_t aio_lio_opcode;   // IO_CMD_*
    int16_t  aio_reqprio;
    uint32_t aio_fildes;

    uint64_t aio_buf;          // buffer (PREAD/PWRITE) or iovec* (PREADV/PWRITEV)
    uint64_t aio_nbytes;       // byte count, or iovec count for the V variants
    int64_t  aio_offset;       // file offset

    uint64_t aio_reserved2;

    uint32_t aio_flags;        // IOCB_FLAG_*
    uint32_t aio_resfd;        // eventfd when IOCB_FLAG_RESFD is set
};

#define IOCB_FLAG_RESFD  (1 << 0)
#define IOCB_FLAG_IOPRIO (1 << 1)

int io_setup(int nr_events, io_context_t *ctxp_idp);
int io_submit(io_context_t ctx, long nr, struct iocb *ios[]);
int io_getevents(io_context_t ctx_id, long min_nr, long nr,
        struct io_event *events, struct timespec *timeout);
int io_destroy(io_context_t ctx);
int io_cancel(io_context_t ctx, struct iocb *iocb, struct io_event *evt);

#ifdef __cplusplus
}
#endif

#endif
