/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// This is the Linux-specific asynchronous I/O API / ABI from libaio.
// Note that this API is different from the POSIX AIO API.
//
// OSv has no user/kernel boundary, so "async" I/O is achieved the same way
// io_uring does it: each submitted iocb is run on a short-lived worker thread
// that performs the blocking read/write/fsync via the VFS, then queues an
// io_event and wakes any thread parked in io_getevents().  A context caps the
// number of in-flight ops at the nr_events requested at io_setup() time.

#include <api/libaio.h>

#include <sys/uio.h>
#include <errno.h>
#include <deque>

#include <osv/export.h>
#include <osv/mutex.h>
#include <osv/condvar.h>
#include <osv/sched.hh>
#include <osv/file.h>
#include <osv/clock.hh>

#include "fs/vfs/vfs.h"

struct io_context {
    mutex                   mtx;
    condvar                 cv;             // signalled when an event completes
    unsigned                max_events;     // capacity requested at io_setup()
    unsigned                inflight = 0;   // ops submitted but not yet retired
    bool                    destroying = false;
    std::deque<io_event>    completed;      // retired events awaiting io_getevents
};

// Run one iocb to completion and record its io_event.  Executed on a worker
// thread so the submitting thread returns immediately.
static void libaio_run_one(struct io_context *ctx, struct iocb *cb)
{
    int64_t res;
    struct file *fp = nullptr;

    if (fget(cb->aio_fildes, &fp) != 0) {
        res = -EBADF;
    } else {
        size_t done = 0;
        int error = 0;
        switch (cb->aio_lio_opcode) {
        case IO_CMD_PREAD: {
            struct iovec iov { reinterpret_cast<void*>(cb->aio_buf),
                               static_cast<size_t>(cb->aio_nbytes) };
            error = sys_read(fp, &iov, 1, cb->aio_offset, &done);
            break;
        }
        case IO_CMD_PWRITE: {
            struct iovec iov { reinterpret_cast<void*>(cb->aio_buf),
                               static_cast<size_t>(cb->aio_nbytes) };
            error = sys_write(fp, &iov, 1, cb->aio_offset, &done);
            break;
        }
        case IO_CMD_PREADV:
            error = sys_read(fp, reinterpret_cast<struct iovec*>(cb->aio_buf),
                             cb->aio_nbytes, cb->aio_offset, &done);
            break;
        case IO_CMD_PWRITEV:
            error = sys_write(fp, reinterpret_cast<struct iovec*>(cb->aio_buf),
                              cb->aio_nbytes, cb->aio_offset, &done);
            break;
        case IO_CMD_FSYNC:
        case IO_CMD_FDSYNC:
            error = sys_fsync(fp);
            done = 0;
            break;
        case IO_CMD_NOOP:
            error = 0;
            done = 0;
            break;
        default:
            error = EINVAL;
            break;
        }
        res = error ? -error : static_cast<int64_t>(done);
        fdrop(fp);
    }

    io_event ev {};
    ev.data = cb->aio_data;
    ev.obj  = reinterpret_cast<uint64_t>(cb);
    ev.res  = res;
    ev.res2 = 0;

    WITH_LOCK(ctx->mtx) {
        ctx->completed.push_back(ev);
        ctx->inflight--;
        ctx->cv.wake_all();
    }

    // Best-effort eventfd notification if the iocb asked for one.
    if (cb->aio_flags & IOCB_FLAG_RESFD) {
        uint64_t val = 1;
        (void)::write(cb->aio_resfd, &val, sizeof(val));
    }
}

OSV_LIBAIO_API
int io_setup(int nr_events, io_context_t *ctxp)
{
    if (nr_events < 0 || !ctxp) {
        return -EINVAL;
    }
    auto *ctx = new (std::nothrow) io_context;
    if (!ctx) {
        return -ENOMEM;
    }
    ctx->max_events = nr_events;
    *ctxp = ctx;
    return 0;
}

OSV_LIBAIO_API
int io_submit(io_context_t ctx, long nr, struct iocb *ios[])
{
    if (!ctx || nr < 0) {
        return -EINVAL;
    }
    if (nr == 0) {
        return 0;
    }

    long submitted = 0;
    for (long i = 0; i < nr; i++) {
        struct iocb *cb = ios[i];
        sched::thread *w;
        WITH_LOCK(ctx->mtx) {
            if (ctx->destroying) {
                break;
            }
            // Respect the context's in-flight capacity.  If we already accepted
            // at least one op, return the partial count (Linux does the same);
            // otherwise signal EAGAIN.
            if (ctx->max_events && ctx->inflight >= ctx->max_events) {
                if (submitted > 0) {
                    return submitted;
                }
                return -EAGAIN;
            }
            ctx->inflight++;
            // ponytail: one detached worker thread per iocb.  Simple and
            // correct; if a caller submits huge batches and thread-create cost
            // shows up, swap in a bounded worker pool like core/io_uring.cc's.
            w = sched::thread::make([ctx, cb]() { libaio_run_one(ctx, cb); },
                    sched::thread::attr().detached().name("libaio"));
        }
        w->start();
        submitted++;
    }
    return submitted > 0 ? submitted : -EINVAL;
}

OSV_LIBAIO_API
int io_getevents(io_context_t ctx, long min_nr, long nr,
        struct io_event *events, struct timespec *timeout)
{
    if (!ctx || min_nr < 0 || nr < 0 || min_nr > nr) {
        return -EINVAL;
    }
    if (nr == 0) {
        return 0;
    }

    // Compute an absolute deadline from the (relative) timeout, if any.
    bool have_deadline = timeout != nullptr;
    auto deadline = osv::clock::uptime::now();
    if (have_deadline) {
        deadline += std::chrono::seconds(timeout->tv_sec) +
                    std::chrono::nanoseconds(timeout->tv_nsec);
    }

    long collected = 0;
    WITH_LOCK(ctx->mtx) {
        while (collected < nr) {
            while (!ctx->completed.empty() && collected < nr) {
                events[collected++] = ctx->completed.front();
                ctx->completed.pop_front();
            }
            if (collected >= min_nr) {
                break;     // satisfied the caller's minimum
            }
            if (have_deadline) {
                if (ctx->cv.wait(&ctx->mtx, deadline) != 0) {
                    break; // timed out
                }
            } else {
                ctx->cv.wait(ctx->mtx);
            }
        }
    }
    return collected;
}

OSV_LIBAIO_API
int io_destroy(io_context_t ctx)
{
    if (!ctx) {
        return -EINVAL;
    }
    WITH_LOCK(ctx->mtx) {
        ctx->destroying = true;
        // Wait for all in-flight ops to retire.  Each worker is detached and
        // self-reaps; once inflight reaches zero no worker still references
        // ctx, so it is safe to free.
        while (ctx->inflight > 0) {
            ctx->cv.wait(ctx->mtx);
        }
    }
    delete ctx;
    return 0;
}

OSV_LIBAIO_API
int io_cancel(io_context_t ctx, struct iocb *iocb, struct io_event *evt)
{
    // An op is executed on a worker thread that runs the blocking VFS call to
    // completion; there is no safe mid-flight cancellation point, so report
    // EINVAL (a valid Linux response when the op cannot be cancelled).
    (void)ctx; (void)iocb; (void)evt;
    return -EINVAL;
}
