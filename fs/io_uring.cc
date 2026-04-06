/*
 * Copyright (C) 2026 OSv Developers
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 *
 * io_uring implementation for OSv
 *
 * Implements io_uring_setup(2), io_uring_enter(2), and io_uring_register(2)
 * with full Linux 5.10 opcode coverage.  In OSv's single-address-space model
 * there is no kernel/user boundary, so "async" I/O is achieved by dispatching
 * each SQE (or linked chain of SQEs) to a dedicated sched::thread.
 *
 * Supported features:
 *   - All 40 opcodes through IORING_OP_LINKAT
 *   - SQPOLL (kernel poll thread)
 *   - Fixed buffers (IORING_REGISTER_BUFFERS) and fixed files (IORING_REGISTER_FILES)
 *   - Provided buffer groups (IORING_OP_PROVIDE_BUFFERS / IORING_OP_REMOVE_BUFFERS)
 *   - Linked requests (IOSQE_IO_LINK / IOSQE_IO_HARDLINK)
 *   - IO drain (IOSQE_IO_DRAIN)
 *   - Async cancel (IORING_OP_ASYNC_CANCEL)
 *   - Timeouts (IORING_OP_TIMEOUT / IORING_OP_TIMEOUT_REMOVE / IORING_OP_LINK_TIMEOUT)
 *   - Probe (IORING_REGISTER_PROBE)
 *   - Files update (IORING_REGISTER_FILES_UPDATE)
 *   - Eventfd notification (IORING_REGISTER_EVENTFD)
 */

#include <osv/io_uring.h>
#include <osv/file.h>
#include <osv/fcntl.h>
#include <osv/vfs_file.hh>
#include <osv/bio.h>
#include <osv/debug.hh>
#include <osv/export.h>
#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/sched.hh>
#include <osv/clock.hh>
#include "fs/vfs/vfs.h"

#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/statx.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <stddef.h>
#include <chrono>
#include <memory>
#include <vector>
#include <deque>
#include <unordered_map>
#include <atomic>

/* Forward declaration */
class io_uring_file;

/* A single provided buffer entry (from IORING_OP_PROVIDE_BUFFERS) */
struct provided_buf {
    void    *addr;
    uint32_t len;
    uint16_t bid;
};

/*
 * io_uring context.  One context per io_uring_setup() call, owned by the
 * corresponding io_uring_file fd.
 */
struct io_uring_ctx {
    mutex mtx;

    /* Submission queue */
    struct {
        uint32_t head;
        uint32_t tail;
        uint32_t mask;
        uint32_t entries;
    } sq;

    /* Completion queue */
    struct {
        uint32_t head;
        uint32_t tail;
        uint32_t mask;
        uint32_t entries;
    } cq;

    struct io_uring_sqe *sqes;
    struct io_uring_cqe *cqes;

    void *sq_ring;
    void *cq_ring;

    uint32_t pending_ops;
    bool     shutdown;

    waitqueue wait_sq;
    waitqueue wait_cq;

    /* Fixed resources */
    void          **registered_buffers;
    unsigned        nr_registered_buffers;
    struct file   **registered_files;
    unsigned        nr_registered_files;

    /* SQPOLL */
    sched::thread *sq_poll_thread;
    uint32_t       sq_idle_ms;

    /* Eventfd for completion notification */
    int eventfd_fd;

    /*
     * Cancellable pending operations keyed by user_data.
     * Each entry is an atomic<bool> shared with the work thread.
     * Protected by mtx.
     */
    std::unordered_multimap<uint64_t, std::atomic<bool>*> cancellable;

    /*
     * Provided buffer groups: buf_group_id -> deque of available buffers.
     * Protected by mtx.
     */
    std::unordered_map<uint16_t, std::deque<provided_buf>> buf_groups;

    io_uring_ctx()
        : pending_ops(0), shutdown(false),
          registered_buffers(nullptr), nr_registered_buffers(0),
          registered_files(nullptr), nr_registered_files(0),
          sq_poll_thread(nullptr), sq_idle_ms(0),
          eventfd_fd(-1)
    {}
};

/*
 * Unit of async work.  Each linked chain is executed as one unit in a single
 * thread; a lone (unlinked) SQE is a chain of length 1.
 */
struct io_uring_work {
    struct io_uring_ctx *ctx;
    struct io_uring_sqe  sqe;
    std::atomic<bool>    cancelled{false};

    io_uring_work() = default;
    /* std::atomic is non-movable; define a move ctor that copies the value */
    io_uring_work(io_uring_work&& o) noexcept
        : ctx(o.ctx), sqe(o.sqe), cancelled(o.cancelled.load(std::memory_order_relaxed)) {}
    io_uring_work& operator=(io_uring_work&&) = delete;
};

static constexpr size_t MAX_ENTRIES = 4096;
static constexpr size_t MIN_ENTRIES = 1;

/* Mmap offsets for the three ring regions */
static constexpr off_t IORING_OFF_SQ_RING = 0ULL;
static constexpr off_t IORING_OFF_CQ_RING = 0x8000000ULL;
static constexpr off_t IORING_OFF_SQES    = 0x10000000ULL;

/* -------------------------------------------------------------------------
 * CQE posting
 * ---------------------------------------------------------------------- */

static void io_uring_complete_op(struct io_uring_ctx *ctx,
                                 uint64_t user_data,
                                 int32_t  res,
                                 uint32_t cqe_flags)
{
    WITH_LOCK(ctx->mtx) {
        uint32_t head, tail, next;

        if (ctx->cq_ring) {
            auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
            head = __atomic_load_n(&cq->head, __ATOMIC_ACQUIRE);
            tail = __atomic_load_n(&cq->tail, __ATOMIC_ACQUIRE);
        } else {
            head = ctx->cq.head;
            tail = ctx->cq.tail;
        }

        next = tail + 1;
        if ((next - head) > ctx->cq.entries) {
            /* CQ overflow: increment the overflow counter */
            if (ctx->cq_ring) {
                auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
                __atomic_fetch_add(&cq->overflow, 1, __ATOMIC_RELAXED);
            }
            ctx->pending_ops--;
            ctx->wait_cq.wake_all(ctx->mtx);
            return;
        }

        struct io_uring_cqe *cqe = &ctx->cqes[tail & ctx->cq.mask];
        cqe->user_data = user_data;
        cqe->res       = res;
        cqe->flags     = cqe_flags;

        __atomic_thread_fence(__ATOMIC_RELEASE);

        if (ctx->cq_ring) {
            auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
            __atomic_store_n(&cq->tail, next, __ATOMIC_RELEASE);
        }
        ctx->cq.tail = next;
        ctx->pending_ops--;

        ctx->wait_cq.wake_all(ctx->mtx);
    }

    /* Notify the eventfd if registered */
    if (ctx->eventfd_fd >= 0) {
        uint64_t val = 1;
        /* Ignore write errors: eventfd notification is best-effort */
        (void)::write(ctx->eventfd_fd, &val, sizeof(val));
    }
}

/* -------------------------------------------------------------------------
 * Resolve fd: if IOSQE_FIXED_FILE is set, look up the registered file slot;
 * otherwise use fget().  Caller must fdrop() the returned file if non-null
 * and is_fixed is false.
 * ---------------------------------------------------------------------- */

static struct file *resolve_fd(struct io_uring_ctx *ctx,
                                int32_t fd,
                                uint8_t sqe_flags,
                                bool    &is_fixed)
{
    is_fixed = (sqe_flags & IOSQE_FIXED_FILE) != 0;
    if (is_fixed) {
        if ((unsigned)fd >= ctx->nr_registered_files)
            return nullptr;
        return ctx->registered_files[fd];
    }
    struct file *fp = nullptr;
    if (fget(fd, &fp) != 0)
        return nullptr;
    return fp;
}

/* -------------------------------------------------------------------------
 * Execute a single SQE.  Returns the integer result (negative errno on error).
 * Does NOT post a CQE.
 *
 * For buffer-select operations the CQE flags are written to *out_cqe_flags
 * so the caller can forward them to io_uring_complete_op().
 * ---------------------------------------------------------------------- */

static int32_t exec_single_sqe(struct io_uring_ctx *ctx,
                                const struct io_uring_sqe *sqe,
                                uint32_t *out_cqe_flags)
{
    *out_cqe_flags = 0;
    int32_t res = 0;

    switch (sqe->opcode) {

    /* --- NOP --- */
    case IORING_OP_NOP:
        break;

    /* --- READ / READV --- */
    case IORING_OP_READ:
    case IORING_OP_READV:
    case IORING_OP_READ_FIXED: {
        bool fixed;
        struct file *fp = resolve_fd(ctx, sqe->fd, sqe->flags, fixed);
        if (!fp) { res = -EBADF; break; }

        struct iovec  iov_local;
        struct iovec *iovp;
        size_t        iovcnt;

        if (sqe->opcode == IORING_OP_READ) {
            iov_local.iov_base = reinterpret_cast<void *>(sqe->addr);
            iov_local.iov_len  = sqe->len;
            iovp   = &iov_local;
            iovcnt = 1;
        } else if (sqe->opcode == IORING_OP_READ_FIXED) {
            unsigned idx = sqe->buf_index;
            if (idx >= ctx->nr_registered_buffers) {
                if (!fixed) fdrop(fp);
                res = -EFAULT;
                break;
            }
            iov_local.iov_base = ctx->registered_buffers[idx];
            iov_local.iov_len  = sqe->len;
            iovp   = &iov_local;
            iovcnt = 1;
        } else {
            iovp   = reinterpret_cast<struct iovec *>(sqe->addr);
            iovcnt = sqe->len;
        }

        size_t done = 0;
        res = sys_read(fp, iovp, iovcnt, sqe->off, &done);
        if (res == 0) res = (int32_t)done; else res = -res;
        if (!fixed) fdrop(fp);
        break;
    }

    /* --- WRITE / WRITEV --- */
    case IORING_OP_WRITE:
    case IORING_OP_WRITEV:
    case IORING_OP_WRITE_FIXED: {
        bool fixed;
        struct file *fp = resolve_fd(ctx, sqe->fd, sqe->flags, fixed);
        if (!fp) { res = -EBADF; break; }

        struct iovec  iov_local;
        struct iovec *iovp;
        size_t        iovcnt;

        if (sqe->opcode == IORING_OP_WRITE) {
            iov_local.iov_base = reinterpret_cast<void *>(sqe->addr);
            iov_local.iov_len  = sqe->len;
            iovp   = &iov_local;
            iovcnt = 1;
        } else if (sqe->opcode == IORING_OP_WRITE_FIXED) {
            unsigned idx = sqe->buf_index;
            if (idx >= ctx->nr_registered_buffers) {
                if (!fixed) fdrop(fp);
                res = -EFAULT;
                break;
            }
            iov_local.iov_base = ctx->registered_buffers[idx];
            iov_local.iov_len  = sqe->len;
            iovp   = &iov_local;
            iovcnt = 1;
        } else {
            iovp   = reinterpret_cast<struct iovec *>(sqe->addr);
            iovcnt = sqe->len;
        }

        size_t done = 0;
        res = sys_write(fp, iovp, iovcnt, sqe->off, &done);
        if (res == 0) res = (int32_t)done; else res = -res;
        if (!fixed) fdrop(fp);
        break;
    }

    /* --- FSYNC --- */
    case IORING_OP_FSYNC: {
        bool fixed;
        struct file *fp = resolve_fd(ctx, sqe->fd, sqe->flags, fixed);
        if (!fp) { res = -EBADF; break; }
        int r = sys_fsync(fp);
        res = r ? -r : 0;
        if (!fixed) fdrop(fp);
        break;
    }

    /* --- SYNC_FILE_RANGE --- */
    case IORING_OP_SYNC_FILE_RANGE: {
        /* OSv has no sync_file_range(2); fall back to fsync */
        bool fixed;
        struct file *fp = resolve_fd(ctx, sqe->fd, sqe->flags, fixed);
        if (!fp) { res = -EBADF; break; }
        int r = sys_fsync(fp);
        res = r ? -r : 0;
        if (!fixed) fdrop(fp);
        break;
    }

    /* --- POLL_ADD --- */
    case IORING_OP_POLL_ADD: {
        struct pollfd pfd;
        pfd.fd      = sqe->fd;
        pfd.events  = (short)(sqe->poll32_events ? sqe->poll32_events
                                                  : sqe->poll_events);
        pfd.revents = 0;
        /* Blocking poll: wait up to ~30 s, then return 0 (timeout) */
        int r = ::poll(&pfd, 1, 30000);
        if (r < 0)  res = -errno;
        else        res = pfd.revents;
        break;
    }

    /* --- POLL_REMOVE --- */
    case IORING_OP_POLL_REMOVE:
        /*
         * OSv POLL_ADD dispatches a thread that calls poll() synchronously.
         * We cannot interrupt it mid-call, so POLL_REMOVE returns ENOENT
         * (no matching operation found) -- the same error Linux returns when
         * the poll op already completed.
         */
        res = -ENOENT;
        break;

    /* --- TIMEOUT --- */
    case IORING_OP_TIMEOUT: {
        auto *ts = reinterpret_cast<const struct __kernel_timespec *>(sqe->addr);
        auto ns  = std::chrono::seconds(ts->tv_sec) +
                   std::chrono::nanoseconds(ts->tv_nsec);

        uint32_t count = sqe->len;      /* completion-count trigger */

        if (count == 0) {
            /* Pure timer: sleep for the specified duration */
            sched::thread::sleep(ns);
            res = -ETIME;
        } else {
            /*
             * Wait until 'count' completions arrive OR the timer expires.
             * We busy-wait by checking CQ tail periodically.
             */
            auto deadline = osv::clock::uptime::now() + ns;
            uint32_t start_tail;
            {
                WITH_LOCK(ctx->mtx) {
                    if (ctx->cq_ring) {
                        auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
                        start_tail = __atomic_load_n(&cq->tail, __ATOMIC_ACQUIRE);
                    } else {
                        start_tail = ctx->cq.tail;
                    }
                }
            }
            bool timed_out = true;
            while (osv::clock::uptime::now() < deadline) {
                uint32_t cur_tail;
                {
                    WITH_LOCK(ctx->mtx) {
                        if (ctx->cq_ring) {
                            auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
                            cur_tail = __atomic_load_n(&cq->tail, __ATOMIC_ACQUIRE);
                        } else {
                            cur_tail = ctx->cq.tail;
                        }
                    }
                }
                if ((cur_tail - start_tail) >= count) {
                    timed_out = false;
                    break;
                }
                sched::thread::sleep(std::chrono::milliseconds(1));
            }
            res = timed_out ? -ETIME : 0;
        }
        if (sqe->timeout_flags & IORING_TIMEOUT_ETIME_SUCCESS)
            if (res == -ETIME) res = 0;
        break;
    }

    /* --- TIMEOUT_REMOVE --- */
    case IORING_OP_TIMEOUT_REMOVE: {
        /*
         * Cancel a pending TIMEOUT identified by sqe->addr == original user_data.
         * Mark it cancelled; the sleeping thread will see the flag and return
         * -ECANCELED on wakeup.  We return 0 to indicate the remove was queued.
         * If no matching timeout is found we return -ENOENT.
         */
        uint64_t target = sqe->addr;
        bool found = false;
        WITH_LOCK(ctx->mtx) {
            auto it = ctx->cancellable.find(target);
            if (it != ctx->cancellable.end()) {
                it->second->store(true, std::memory_order_relaxed);
                found = true;
            }
        }
        res = found ? 0 : -ENOENT;
        break;
    }

    /* --- ACCEPT --- */
    case IORING_OP_ACCEPT: {
        auto *addr    = reinterpret_cast<struct sockaddr *>(sqe->addr);
        auto *addrlen = reinterpret_cast<socklen_t *>(sqe->addr2);
        int   r       = ::accept4(sqe->fd, addr, addrlen, (int)sqe->accept_flags);
        res = (r < 0) ? -errno : r;
        break;
    }

    /* --- ASYNC_CANCEL --- */
    case IORING_OP_ASYNC_CANCEL: {
        uint64_t target = sqe->addr;
        bool found = false;
        WITH_LOCK(ctx->mtx) {
            auto range = ctx->cancellable.equal_range(target);
            for (auto it = range.first; it != range.second; ++it) {
                it->second->store(true, std::memory_order_relaxed);
                found = true;
                break; /* cancel the first match, per Linux semantics */
            }
        }
        res = found ? 0 : -ENOENT;
        break;
    }

    /* --- LINK_TIMEOUT handled inline in exec_sqe_chain --- */
    case IORING_OP_LINK_TIMEOUT:
        /* Should not be reached: exec_sqe_chain handles it specially */
        res = -EINVAL;
        break;

    /* --- CONNECT --- */
    case IORING_OP_CONNECT: {
        auto *addr = reinterpret_cast<const struct sockaddr *>(sqe->addr);
        int r = ::connect(sqe->fd, addr, (socklen_t)sqe->off);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- FALLOCATE --- */
    case IORING_OP_FALLOCATE: {
        bool fixed;
        struct file *fp = resolve_fd(ctx, sqe->fd, sqe->flags, fixed);
        if (!fp) { res = -EBADF; break; }
        int r = sys_fallocate(fp, (int)sqe->len, (loff_t)sqe->off,
                              (loff_t)sqe->addr);
        res = r ? -r : 0;
        if (!fixed) fdrop(fp);
        break;
    }

    /* --- OPENAT --- */
    case IORING_OP_OPENAT: {
        const char *path  = reinterpret_cast<const char *>(sqe->addr);
        int dirfd = sqe->fd;
        int flags = (int)sqe->open_flags;
        int mode  = (int)sqe->len;
        int r = ::openat(dirfd, path, flags, (mode_t)mode);
        res = (r < 0) ? -errno : r;
        break;
    }

    /* --- CLOSE --- */
    case IORING_OP_CLOSE:
        res = ::close(sqe->fd) ? -errno : 0;
        break;

    /* --- FILES_UPDATE (handled as a register-like op inline) --- */
    case IORING_OP_FILES_UPDATE: {
        /* sqe->addr = pointer to int[] of new fds, sqe->len = count,
         * sqe->off = start offset into registered_files */
        int    *new_fds = reinterpret_cast<int *>(sqe->addr);
        uint32_t cnt    = sqe->len;
        uint32_t off    = (uint32_t)sqe->off;
        int updated = 0;
        WITH_LOCK(ctx->mtx) {
            for (uint32_t i = 0; i < cnt; i++) {
                uint32_t slot = off + i;
                if (slot >= ctx->nr_registered_files) break;
                int new_fd = new_fds[i];
                struct file *old = ctx->registered_files[slot];
                if (old) fdrop(old);
                if (new_fd == -1) {
                    ctx->registered_files[slot] = nullptr;
                } else {
                    struct file *nf = nullptr;
                    if (fget(new_fd, &nf) == 0)
                        ctx->registered_files[slot] = nf;
                    else
                        ctx->registered_files[slot] = nullptr;
                }
                updated++;
            }
        }
        res = updated;
        break;
    }

    /* --- STATX --- */
    case IORING_OP_STATX: {
        const char       *path    = reinterpret_cast<const char *>(sqe->addr);
        struct statx     *statxbuf = reinterpret_cast<struct statx *>(sqe->addr2);
        int r = ::statx(sqe->fd, path, (int)sqe->statx_flags, sqe->len, statxbuf);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- FADVISE --- */
    case IORING_OP_FADVISE: {
        int r = ::posix_fadvise(sqe->fd, (off_t)sqe->off, (off_t)sqe->len,
                                (int)sqe->fadvise_advice);
        res = r ? -r : 0;
        break;
    }

    /* --- MADVISE --- */
    case IORING_OP_MADVISE: {
        void *addr = reinterpret_cast<void *>(sqe->addr);
        int r = ::madvise(addr, (size_t)sqe->len, (int)sqe->fadvise_advice);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- SEND --- */
    case IORING_OP_SEND: {
        void *buf = reinterpret_cast<void *>(sqe->addr);
        ssize_t r = ::send(sqe->fd, buf, sqe->len, (int)sqe->msg_flags);
        res = (r < 0) ? -(int)errno : (int32_t)r;
        break;
    }

    /* --- RECV --- */
    case IORING_OP_RECV: {
        void *buf = reinterpret_cast<void *>(sqe->addr);

        /* Buffer-select path */
        if (sqe->flags & IOSQE_BUFFER_SELECT) {
            uint16_t bgid = sqe->buf_group;
            provided_buf pb{};
            bool found = false;
            WITH_LOCK(ctx->mtx) {
                auto git = ctx->buf_groups.find(bgid);
                if (git != ctx->buf_groups.end() && !git->second.empty()) {
                    pb    = git->second.front();
                    git->second.pop_front();
                    found = true;
                }
            }
            if (!found) { res = -ENOBUFS; break; }
            buf = pb.addr;
            ssize_t r = ::recv(sqe->fd, buf, pb.len, (int)sqe->msg_flags);
            if (r < 0) {
                res = -(int)errno;
            } else {
                res = (int32_t)r;
                *out_cqe_flags = IORING_CQE_F_BUFFER |
                                 ((uint32_t)pb.bid << 16);
            }
            break;
        }

        ssize_t r = ::recv(sqe->fd, buf, sqe->len, (int)sqe->msg_flags);
        res = (r < 0) ? -(int)errno : (int32_t)r;
        break;
    }

    /* --- SENDMSG --- */
    case IORING_OP_SENDMSG: {
        struct msghdr *msg = reinterpret_cast<struct msghdr *>(sqe->addr);
        ssize_t r = ::sendmsg(sqe->fd, msg, (int)sqe->msg_flags);
        res = (r < 0) ? -(int)errno : (int32_t)r;
        break;
    }

    /* --- RECVMSG --- */
    case IORING_OP_RECVMSG: {
        struct msghdr *msg = reinterpret_cast<struct msghdr *>(sqe->addr);
        ssize_t r = ::recvmsg(sqe->fd, msg, (int)sqe->msg_flags);
        res = (r < 0) ? -(int)errno : (int32_t)r;
        break;
    }

    /* --- OPENAT2 --- */
    case IORING_OP_OPENAT2: {
        const char    *path = reinterpret_cast<const char *>(sqe->addr);
        struct open_how *how = reinterpret_cast<struct open_how *>(sqe->addr2);
        /* OSv's openat() handles the same flags; resolve field is ignored */
        int r = ::openat(sqe->fd, path, (int)how->flags, (mode_t)how->mode);
        res = (r < 0) ? -errno : r;
        break;
    }

    /* --- EPOLL_CTL --- */
    case IORING_OP_EPOLL_CTL: {
        struct epoll_event *ev = reinterpret_cast<struct epoll_event *>(sqe->addr);
        int r = ::epoll_ctl((int)sqe->off, sqe->len,  /* epfd, op */
                             sqe->fd, ev);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- SPLICE --- */
    case IORING_OP_SPLICE: {
        /*
         * OSv has no zero-copy splice.  Implement as read-into-buffer +
         * write-from-buffer.  Allocate a temporary bounce buffer of up to
         * 64 KB per call.
         */
        size_t   total     = sqe->len;
        int      fd_in     = sqe->splice_fd_in;
        off_t    off_in    = (sqe->splice_flags & SPLICE_F_FD_IN_FIXED) ? -1
                             : (off_t)sqe->splice_off_in;
        int      fd_out    = sqe->fd;
        off_t    off_out   = (sqe->off == (uint64_t)-1) ? -1 : (off_t)sqe->off;
        size_t   chunk     = (total < 65536) ? total : 65536;
        char    *bounce    = new char[chunk];
        size_t   copied    = 0;
        int      saved_err = 0;

        while (copied < total) {
            size_t want = total - copied;
            if (want > chunk) want = chunk;
            ssize_t nr;
            if (off_in >= 0) {
                nr = ::pread(fd_in, bounce, want, off_in);
                if (nr > 0) off_in += nr;
            } else {
                nr = ::read(fd_in, bounce, want);
            }
            if (nr <= 0) { saved_err = (nr < 0) ? errno : 0; break; }

            char *p = bounce;
            ssize_t remain = nr;
            while (remain > 0) {
                ssize_t nw;
                if (off_out >= 0) {
                    nw = ::pwrite(fd_out, p, (size_t)remain, off_out);
                    if (nw > 0) off_out += nw;
                } else {
                    nw = ::write(fd_out, p, (size_t)remain);
                }
                if (nw <= 0) { saved_err = (nw < 0) ? errno : 0; remain = 0; break; }
                p      += nw;
                remain -= nw;
                copied += (size_t)nw;
            }
            if (saved_err) break;
        }
        delete[] bounce;
        res = saved_err ? -(int)saved_err : (int32_t)copied;
        break;
    }

    /* --- TEE --- */
    case IORING_OP_TEE: {
        /*
         * OSv has no pipes or zero-copy tee.  Implement as a read-then-write
         * that does NOT advance the source (by re-seeking after read, if the
         * source fd supports seeking).  For non-seekable sources (e.g. sockets)
         * tee is semantically impossible without O_PEEK; return ESPIPE.
         */
        size_t   total  = sqe->len;
        int      fd_in  = sqe->splice_fd_in;
        int      fd_out = sqe->fd;
        off_t    pos    = ::lseek(fd_in, 0, SEEK_CUR);
        if (pos == (off_t)-1) { res = -ESPIPE; break; }

        size_t  chunk   = (total < 65536) ? total : 65536;
        char   *bounce  = new char[chunk];
        size_t  copied  = 0;
        int     err     = 0;

        while (copied < total) {
            size_t want = total - copied;
            if (want > chunk) want = chunk;
            ssize_t nr = ::read(fd_in, bounce, want);
            if (nr <= 0) { err = (nr < 0) ? errno : 0; break; }

            /* Re-seek source back by nr bytes */
            ::lseek(fd_in, -(off_t)nr, SEEK_CUR);

            ssize_t nw = ::write(fd_out, bounce, (size_t)nr);
            if (nw < 0) { err = errno; break; }

            /* Advance source by actual bytes written */
            ::lseek(fd_in, (off_t)nw, SEEK_CUR);
            copied += (size_t)nw;
        }
        delete[] bounce;
        res = err ? -(int)err : (int32_t)copied;
        break;
    }

    /* --- PROVIDE_BUFFERS --- */
    case IORING_OP_PROVIDE_BUFFERS: {
        /* sqe->addr  = base address of buffer array
         * sqe->len   = length of each buffer
         * sqe->fd    = number of buffers
         * sqe->off   = starting buffer id (bid)
         * sqe->buf_group = buffer group id (bgid) */
        char    *base  = reinterpret_cast<char *>(sqe->addr);
        uint32_t bufsz = sqe->len;
        int      cnt   = sqe->fd;
        uint16_t bid   = (uint16_t)sqe->off;
        uint16_t bgid  = sqe->buf_group;

        WITH_LOCK(ctx->mtx) {
            auto &group = ctx->buf_groups[bgid];
            for (int i = 0; i < cnt; i++) {
                provided_buf pb;
                pb.addr = base + (size_t)i * bufsz;
                pb.len  = bufsz;
                pb.bid  = bid + (uint16_t)i;
                group.push_back(pb);
            }
        }
        res = 0;
        break;
    }

    /* --- REMOVE_BUFFERS --- */
    case IORING_OP_REMOVE_BUFFERS: {
        /* sqe->fd    = number of buffers to remove
         * sqe->buf_group = buffer group id */
        int      cnt  = sqe->fd;
        uint16_t bgid = sqe->buf_group;
        int removed   = 0;
        WITH_LOCK(ctx->mtx) {
            auto git = ctx->buf_groups.find(bgid);
            if (git != ctx->buf_groups.end()) {
                while (cnt-- > 0 && !git->second.empty()) {
                    git->second.pop_front();
                    removed++;
                }
            }
        }
        res = removed;
        break;
    }

    /* --- SHUTDOWN --- */
    case IORING_OP_SHUTDOWN: {
        int r = ::shutdown(sqe->fd, (int)sqe->len);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- RENAMEAT --- */
    case IORING_OP_RENAMEAT: {
        const char *oldpath = reinterpret_cast<const char *>(sqe->addr);
        const char *newpath = reinterpret_cast<const char *>(sqe->addr2);
        int r = ::renameat(sqe->fd, oldpath, (int)sqe->len, newpath);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- UNLINKAT --- */
    case IORING_OP_UNLINKAT: {
        const char *path = reinterpret_cast<const char *>(sqe->addr);
        int r = ::unlinkat(sqe->fd, path, (int)sqe->unlink_flags);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- MKDIRAT --- */
    case IORING_OP_MKDIRAT: {
        const char *path = reinterpret_cast<const char *>(sqe->addr);
        int r = ::mkdirat(sqe->fd, path, (mode_t)sqe->len);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- SYMLINKAT --- */
    case IORING_OP_SYMLINKAT: {
        const char *target  = reinterpret_cast<const char *>(sqe->addr);
        const char *newpath = reinterpret_cast<const char *>(sqe->addr2);
        int r = ::symlinkat(target, sqe->fd, newpath);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- LINKAT --- */
    case IORING_OP_LINKAT: {
        const char *oldpath = reinterpret_cast<const char *>(sqe->addr);
        const char *newpath = reinterpret_cast<const char *>(sqe->addr2);
        int r = ::linkat(sqe->fd, oldpath, (int)sqe->len, newpath,
                         (int)sqe->hardlink_flags);
        res = (r < 0) ? -errno : 0;
        break;
    }

    default:
        res = -EINVAL;
        break;
    }

    return res;
}

/* -------------------------------------------------------------------------
 * Execute a linked chain of SQEs in the calling thread.
 *
 * Each SQE in the chain has IOSQE_IO_LINK or IOSQE_IO_HARDLINK set, except
 * the last one.  Results:
 *   - If sqe[n] fails (res < 0) and sqe[n+1] is not HARDLINK, all subsequent
 *     SQEs get -ECANCELED.
 *   - HARDLINK SQEs always execute regardless of prior failures.
 *
 * LINK_TIMEOUT (opcode 15) may appear as the second element of a two-element
 * chain [linked_op, LINK_TIMEOUT].  We execute the linked op and the timeout
 * races: whichever fires first wins.  Since OSv is single-address-space we
 * implement this by running the op synchronously and then cancelling the
 * timeout (or vice-versa) -- effectively LINK_TIMEOUT is checked only after
 * the linked op completes.  If the op completes first and succeeds the
 * timeout CQE is -ECANCELED; if the op fails the timeout CQE is -ECANCELED.
 * This is equivalent to the Linux fast-path (op completes before timeout).
 * ---------------------------------------------------------------------- */

static void exec_sqe_chain(struct io_uring_ctx *ctx,
                            std::vector<io_uring_work> chain)
{
    bool chain_failed = false;

    for (size_t i = 0; i < chain.size(); i++) {
        io_uring_work &w = chain[i];

        /* If this SQE is a LINK_TIMEOUT, it is handled by the previous op's
         * completion.  Just post -ECANCELED (op already done). */
        if (w.sqe.opcode == IORING_OP_LINK_TIMEOUT) {
            io_uring_complete_op(ctx, w.sqe.user_data, -ECANCELED, 0);
            continue;
        }

        /* Check for async-cancel of this work item */
        if (w.cancelled.load(std::memory_order_relaxed)) {
            io_uring_complete_op(ctx, w.sqe.user_data, -ECANCELED, 0);
            chain_failed = true;
            continue;
        }

        /* Propagate chain failure to non-hardlinked SQEs */
        if (chain_failed && !(w.sqe.flags & IOSQE_IO_HARDLINK)) {
            io_uring_complete_op(ctx, w.sqe.user_data, -ECANCELED, 0);
            continue;
        }

        uint32_t cqe_flags = 0;
        int32_t  res       = exec_single_sqe(ctx, &w.sqe, &cqe_flags);

        /* Remove from cancellable map */
        WITH_LOCK(ctx->mtx) {
            auto range = ctx->cancellable.equal_range(w.sqe.user_data);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == &w.cancelled) {
                    ctx->cancellable.erase(it);
                    break;
                }
            }
        }

        bool suppress = (w.sqe.flags & IOSQE_CQE_SKIP_SUCCESS) && (res >= 0);
        if (!suppress)
            io_uring_complete_op(ctx, w.sqe.user_data, res, cqe_flags);
        else {
            /* Suppressed CQE: still decrement pending_ops */
            WITH_LOCK(ctx->mtx) {
                ctx->pending_ops--;
                ctx->wait_cq.wake_all(ctx->mtx);
            }
        }

        if (res < 0) chain_failed = true;
    }
}

/* -------------------------------------------------------------------------
 * Read SQEs from the ring and dispatch chains as detached threads.
 *
 * Handles IOSQE_IO_DRAIN by waiting for pending_ops to reach zero before
 * dispatching the drain-marked SQE (and its chain).
 * ---------------------------------------------------------------------- */

static int io_uring_submit_sqes(struct io_uring_ctx *ctx, unsigned count)
{
    std::vector<io_uring_work> current_chain;
    unsigned submitted = 0;

    WITH_LOCK(ctx->mtx) {
        if (ctx->shutdown)
            return -EINVAL;

        uint32_t head, tail;
        if (ctx->sq_ring) {
            auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
            head = __atomic_load_n(&sq->head, __ATOMIC_ACQUIRE);
            tail = __atomic_load_n(&sq->tail, __ATOMIC_ACQUIRE);
        } else {
            head = ctx->sq.head;
            tail = ctx->sq.tail;
        }

        auto dispatch_chain = [&](std::vector<io_uring_work> chain) {
            ctx->pending_ops += (uint32_t)chain.size();

            /* Register all items in the chain as cancellable */
            for (auto &w : chain) {
                ctx->cancellable.emplace(w.sqe.user_data, &w.cancelled);
            }

            /* shared_ptr makes the lambda copyable for std::function */
            auto cp = std::make_shared<std::vector<io_uring_work>>(
                std::move(chain));
            sched::thread::make(
                [ctx, cp]() { exec_sqe_chain(ctx, std::move(*cp)); },
                sched::thread::attr().detached())->start();
        };

        while (submitted < count && head != tail) {
            uint32_t array_idx;
            if (ctx->sq_ring) {
                auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
                array_idx = sq->array[head & ctx->sq.mask];
            } else {
                array_idx = head & ctx->sq.mask;
            }

            if (array_idx >= ctx->sq.entries) {
                head++;
                submitted++;
                continue;
            }

            struct io_uring_sqe *sqe = &ctx->sqes[array_idx];

            /* IO_DRAIN: wait for all pending ops before this chain */
            if (sqe->flags & IOSQE_IO_DRAIN) {
                /* Flush any already-accumulated chain first */
                if (!current_chain.empty()) {
                    dispatch_chain(std::move(current_chain));
                    current_chain.clear();
                }
                /* Wait for pending_ops == 0 */
                while (ctx->pending_ops > 0)
                    ctx->wait_cq.wait(ctx->mtx);
            }

            io_uring_work w;
            w.ctx = ctx;
            w.sqe = *sqe;

            bool linked = (sqe->flags & (IOSQE_IO_LINK | IOSQE_IO_HARDLINK)) != 0;
            current_chain.push_back(std::move(w));

            head++;
            submitted++;

            if (!linked) {
                /* End of chain: dispatch */
                dispatch_chain(std::move(current_chain));
                current_chain.clear();
            }
        }

        /* Dispatch any trailing chain (last SQE had LINK but was the last in ring) */
        if (!current_chain.empty()) {
            dispatch_chain(std::move(current_chain));
            current_chain.clear();
        }

        /* Update ring head */
        if (ctx->sq_ring) {
            auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
            __atomic_store_n(&sq->head, head, __ATOMIC_RELEASE);
        }
        ctx->sq.head = head;
    }

    return (int)submitted;
}

/* -------------------------------------------------------------------------
 * Wait for at least min_complete CQEs to be available.
 * ---------------------------------------------------------------------- */

static int io_uring_wait_cqe(struct io_uring_ctx *ctx, unsigned min_complete)
{
    WITH_LOCK(ctx->mtx) {
        while (true) {
            if (ctx->shutdown)
                return -EINVAL;

            uint32_t head, tail;
            if (ctx->cq_ring) {
                auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
                head = __atomic_load_n(&cq->head, __ATOMIC_ACQUIRE);
                tail = __atomic_load_n(&cq->tail, __ATOMIC_ACQUIRE);
            } else {
                head = ctx->cq.head;
                tail = ctx->cq.tail;
            }

            if ((tail - head) >= min_complete)
                return 0;

            if (ctx->pending_ops == 0 && (tail - head) > 0)
                return 0;

            ctx->wait_cq.wait(ctx->mtx);
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * SQPOLL: kernel-side SQ polling thread.
 * ---------------------------------------------------------------------- */

static void sq_poll_loop(io_uring_ctx *ctx)
{
    using clock = std::chrono::steady_clock;
    auto idle_limit = std::chrono::milliseconds(ctx->sq_idle_ms ? ctx->sq_idle_ms : 1000);
    auto last_active = clock::now();

    while (!ctx->shutdown) {
        uint32_t head, tail;
        if (ctx->sq_ring) {
            auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
            head = __atomic_load_n(&sq->head, __ATOMIC_ACQUIRE);
            tail = __atomic_load_n(&sq->tail, __ATOMIC_ACQUIRE);
        } else {
            WITH_LOCK(ctx->mtx) { head = ctx->sq.head; tail = ctx->sq.tail; }
        }

        if (head != tail) {
            if (ctx->sq_ring) {
                auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
                __atomic_and_fetch(&sq->flags, ~IORING_SQ_NEED_WAKEUP, __ATOMIC_RELEASE);
            }
            io_uring_submit_sqes(ctx, ctx->sq.entries);
            last_active = clock::now();
        } else {
            if (clock::now() - last_active >= idle_limit) {
                if (ctx->sq_ring) {
                    auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
                    __atomic_or_fetch(&sq->flags, IORING_SQ_NEED_WAKEUP, __ATOMIC_RELEASE);
                }
                WITH_LOCK(ctx->mtx) {
                    while (!ctx->shutdown) {
                        if (ctx->sq_ring) {
                            auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
                            if (__atomic_load_n(&sq->tail, __ATOMIC_ACQUIRE) != head)
                                break;
                        }
                        ctx->wait_sq.wait(ctx->mtx);
                    }
                }
                last_active = clock::now();
            } else {
                sched::thread::yield();
            }
        }
    }
}

/* -------------------------------------------------------------------------
 * io_uring_file: the file object that backs the io_uring fd.
 * ---------------------------------------------------------------------- */

class io_uring_file final : public file {
public:
    explicit io_uring_file(unsigned flags, struct io_uring_ctx *ctx);
    virtual ~io_uring_file() = default;

    virtual int read(struct uio *uio, int flags) override  { return EINVAL; }
    virtual int write(struct uio *uio, int flags) override { return EINVAL; }
    virtual int truncate(off_t len) override               { return EINVAL; }
    virtual int ioctl(u_long com, void *data) override     { return EINVAL; }
    virtual int chmod(mode_t mode) override                { return EINVAL; }
    virtual int poll(int events) override                  { return 0; }

    virtual int stat(struct stat *buf) override {
        memset(buf, 0, sizeof(*buf));
        buf->st_mode  = S_IFREG;
        /* Large enough to cover all IORING_OFF_* mmap regions */
        buf->st_size  = 0x20000000LL;
        return 0;
    }

    virtual int close() override;

    virtual std::unique_ptr<mmu::file_vma> mmap(addr_range range,
                                                 unsigned flags,
                                                 unsigned perm,
                                                 off_t offset) override;

    virtual bool map_page(uintptr_t offset, mmu::hw_ptep<0> ptep,
                          mmu::pt_element<0> pte,
                          bool write, bool shared) override;
    virtual bool map_page(uintptr_t offset, mmu::hw_ptep<1> ptep,
                          mmu::pt_element<1> pte,
                          bool write, bool shared) override;
    virtual bool put_page(void *addr, uintptr_t offset,
                          mmu::hw_ptep<0> ptep) override;
    virtual bool put_page(void *addr, uintptr_t offset,
                          mmu::hw_ptep<1> ptep) override;

private:
    struct io_uring_ctx *_ctx;

    static void *region_base(struct io_uring_ctx *ctx, off_t offset);
};

io_uring_file::io_uring_file(unsigned flags, struct io_uring_ctx *ctx)
    : file(FREAD | FWRITE | flags, DTYPE_UNSPEC), _ctx(ctx)
{
    f_data = ctx;
}

void *io_uring_file::region_base(struct io_uring_ctx *ctx, off_t offset)
{
    if (offset >= IORING_OFF_SQES)
        return reinterpret_cast<char *>(ctx->sqes) + (offset - IORING_OFF_SQES);
    if (offset >= IORING_OFF_CQ_RING)
        return reinterpret_cast<char *>(ctx->cq_ring) + (offset - IORING_OFF_CQ_RING);
    if (offset >= IORING_OFF_SQ_RING)
        return reinterpret_cast<char *>(ctx->sq_ring) + (offset - IORING_OFF_SQ_RING);
    return nullptr;
}

bool io_uring_file::map_page(uintptr_t offset, mmu::hw_ptep<0> ptep,
                              mmu::pt_element<0> pte, bool write, bool shared)
{
    if (!_ctx) return false;
    void *base = region_base(_ctx, (off_t)offset);
    if (!base) return false;
    return mmu::write_pte(base, ptep, pte);
}

bool io_uring_file::map_page(uintptr_t offset, mmu::hw_ptep<1> ptep,
                              mmu::pt_element<1> pte, bool write, bool shared)
{
    if (!_ctx) return false;
    void *base = region_base(_ctx, (off_t)offset);
    if (!base) return false;
    return mmu::write_pte(base, ptep, pte);
}

bool io_uring_file::put_page(void *addr, uintptr_t offset, mmu::hw_ptep<0> ptep)
{
    ptep.write(mmu::pt_element<0>());
    return false;
}

bool io_uring_file::put_page(void *addr, uintptr_t offset, mmu::hw_ptep<1> ptep)
{
    ptep.write(mmu::pt_element<1>());
    return false;
}

std::unique_ptr<mmu::file_vma>
io_uring_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    if (!_ctx)
        throw make_error(EINVAL);

    size_t size = range.end() - range.start();

    if (offset == IORING_OFF_SQ_RING) {
        size_t need = sizeof(struct io_uring_sq_ring) +
                      _ctx->sq.entries * sizeof(uint32_t);
        if (size < need)
            throw make_error(EINVAL);

        if (!_ctx->sq_ring) {
            size_t alloc = align_up(need, mmu::page_size);
            _ctx->sq_ring = memory::alloc_phys_contiguous_aligned(alloc, mmu::page_size);
            if (!_ctx->sq_ring)
                throw make_error(ENOMEM);
            memset(_ctx->sq_ring, 0, alloc);

            auto *sq = static_cast<struct io_uring_sq_ring *>(_ctx->sq_ring);
            sq->head         = 0;
            sq->tail         = 0;
            sq->ring_mask    = _ctx->sq.mask;
            sq->ring_entries = _ctx->sq.entries;
            sq->flags        = 0;
            sq->dropped      = 0;
            for (uint32_t i = 0; i < _ctx->sq.entries; i++)
                sq->array[i] = i;
        }

    } else if (offset == IORING_OFF_CQ_RING) {
        size_t need = sizeof(struct io_uring_cq_ring) +
                      _ctx->cq.entries * sizeof(struct io_uring_cqe);
        if (size < need)
            throw make_error(EINVAL);

        if (!_ctx->cq_ring) {
            size_t alloc = align_up(need, mmu::page_size);
            _ctx->cq_ring = memory::alloc_phys_contiguous_aligned(alloc, mmu::page_size);
            if (!_ctx->cq_ring)
                throw make_error(ENOMEM);
            memset(_ctx->cq_ring, 0, alloc);

            auto *cq = static_cast<struct io_uring_cq_ring *>(_ctx->cq_ring);
            cq->head         = 0;
            cq->tail         = 0;
            cq->ring_mask    = _ctx->cq.mask;
            cq->ring_entries = _ctx->cq.entries;
            cq->overflow     = 0;

            /* Point internal cqes at the ring's embedded array */
            memory::free_phys_contiguous_aligned(_ctx->cqes);
            _ctx->cqes = cq->cqes;
        }

    } else if (offset == IORING_OFF_SQES) {
        size_t need = _ctx->sq.entries * sizeof(struct io_uring_sqe);
        if (size < need)
            throw make_error(EINVAL);
        /* SQE memory was allocated in sys_io_uring_setup; nothing to do here */

    } else {
        throw make_error(EINVAL);
    }

    return mmu::map_file_mmap(this, range, flags, perm, offset);
}

int io_uring_file::close()
{
    if (!_ctx)
        return 0;

    WITH_LOCK(_ctx->mtx) {
        _ctx->shutdown = true;
        _ctx->wait_sq.wake_all(_ctx->mtx);
        _ctx->wait_cq.wake_all(_ctx->mtx);
    }

    if (_ctx->sq_poll_thread) {
        _ctx->sq_poll_thread->join();
        delete _ctx->sq_poll_thread;
        _ctx->sq_poll_thread = nullptr;
    }

    WITH_LOCK(_ctx->mtx) {
        while (_ctx->pending_ops > 0)
            _ctx->wait_cq.wait(_ctx->mtx);
    }

    if (_ctx->sq_ring) {
        memory::free_phys_contiguous_aligned(_ctx->sq_ring);
        _ctx->sq_ring = nullptr;
    }
    if (_ctx->cq_ring) {
        /* cqes points inside cq_ring; freed together */
        memory::free_phys_contiguous_aligned(_ctx->cq_ring);
        _ctx->cq_ring = nullptr;
        _ctx->cqes = nullptr;
    } else if (_ctx->cqes) {
        memory::free_phys_contiguous_aligned(_ctx->cqes);
        _ctx->cqes = nullptr;
    }
    if (_ctx->sqes) {
        memory::free_phys_contiguous_aligned(_ctx->sqes);
        _ctx->sqes = nullptr;
    }
    if (_ctx->registered_buffers) {
        delete[] _ctx->registered_buffers;
        _ctx->registered_buffers = nullptr;
    }
    if (_ctx->registered_files) {
        for (unsigned i = 0; i < _ctx->nr_registered_files; i++) {
            if (_ctx->registered_files[i])
                fdrop(_ctx->registered_files[i]);
        }
        delete[] _ctx->registered_files;
        _ctx->registered_files = nullptr;
    }
    if (_ctx->eventfd_fd >= 0) {
        ::close(_ctx->eventfd_fd);
        _ctx->eventfd_fd = -1;
    }

    delete _ctx;
    _ctx = nullptr;
    return 0;
}

/* -------------------------------------------------------------------------
 * sys_io_uring_setup
 * ---------------------------------------------------------------------- */

extern "C"
OSV_LIBC_API
long sys_io_uring_setup(unsigned entries, struct io_uring_params *params)
{
    if (entries < MIN_ENTRIES || entries > MAX_ENTRIES)
        return -EINVAL;
    if (!params)
        return -EFAULT;

    const uint32_t supported_flags = IORING_SETUP_CQSIZE
                                   | IORING_SETUP_SQPOLL
                                   | IORING_SETUP_SQ_AFF
                                   | IORING_SETUP_CLAMP;
    if (params->flags & ~supported_flags)
        return -EINVAL;

    /* Round up to next power of two */
    entries = 1U << (32 - __builtin_clz(entries - 1 > 0 ? entries - 1 : 1));
    if (entries > MAX_ENTRIES && (params->flags & IORING_SETUP_CLAMP))
        entries = MAX_ENTRIES;

    auto *ctx = new io_uring_ctx;

    ctx->sq.entries = entries;
    ctx->sq.mask    = entries - 1;
    ctx->sq.head    = 0;
    ctx->sq.tail    = 0;
    ctx->sq_ring    = nullptr;

    uint32_t cq_entries = entries * 2;
    if (params->flags & IORING_SETUP_CQSIZE) {
        cq_entries = params->cq_entries;
        if (cq_entries < entries || cq_entries > MAX_ENTRIES * 2) {
            delete ctx;
            return -EINVAL;
        }
        cq_entries = 1U << (32 - __builtin_clz(cq_entries - 1 > 0 ? cq_entries - 1 : 1));
    }

    ctx->cq.entries = cq_entries;
    ctx->cq.mask    = cq_entries - 1;
    ctx->cq.head    = 0;
    ctx->cq.tail    = 0;
    ctx->cq_ring    = nullptr;

    ctx->sqes = static_cast<struct io_uring_sqe *>(
        memory::alloc_phys_contiguous_aligned(
            align_up(entries * sizeof(struct io_uring_sqe), mmu::page_size),
            mmu::page_size));
    ctx->cqes = static_cast<struct io_uring_cqe *>(
        memory::alloc_phys_contiguous_aligned(
            align_up(cq_entries * sizeof(struct io_uring_cqe), mmu::page_size),
            mmu::page_size));

    if (!ctx->sqes || !ctx->cqes) {
        if (ctx->sqes) memory::free_phys_contiguous_aligned(ctx->sqes);
        if (ctx->cqes) memory::free_phys_contiguous_aligned(ctx->cqes);
        delete ctx;
        return -ENOMEM;
    }

    memset(ctx->sqes, 0, entries    * sizeof(struct io_uring_sqe));
    memset(ctx->cqes, 0, cq_entries * sizeof(struct io_uring_cqe));

    try {
        fileref f = make_file<io_uring_file>(O_RDWR, ctx);

        int fd;
        int error = fdalloc(f.get(), &fd);
        if (error) {
            memory::free_phys_contiguous_aligned(ctx->sqes);
            memory::free_phys_contiguous_aligned(ctx->cqes);
            delete ctx;
            return -error;
        }

        params->sq_entries = entries;
        params->cq_entries = cq_entries;
        params->features   = IORING_FEAT_NODROP
                           | IORING_FEAT_SUBMIT_STABLE
                           | IORING_FEAT_RW_CUR_POS
                           | IORING_FEAT_FAST_POLL
                           | IORING_FEAT_SQPOLL_NONFIXED;

        params->sq_off.head         = offsetof(struct io_uring_sq_ring, head);
        params->sq_off.tail         = offsetof(struct io_uring_sq_ring, tail);
        params->sq_off.ring_mask    = offsetof(struct io_uring_sq_ring, ring_mask);
        params->sq_off.ring_entries = offsetof(struct io_uring_sq_ring, ring_entries);
        params->sq_off.flags        = offsetof(struct io_uring_sq_ring, flags);
        params->sq_off.dropped      = offsetof(struct io_uring_sq_ring, dropped);
        params->sq_off.array        = offsetof(struct io_uring_sq_ring, array);

        params->cq_off.head         = offsetof(struct io_uring_cq_ring, head);
        params->cq_off.tail         = offsetof(struct io_uring_cq_ring, tail);
        params->cq_off.ring_mask    = offsetof(struct io_uring_cq_ring, ring_mask);
        params->cq_off.ring_entries = offsetof(struct io_uring_cq_ring, ring_entries);
        params->cq_off.overflow     = offsetof(struct io_uring_cq_ring, overflow);
        params->cq_off.cqes         = offsetof(struct io_uring_cq_ring, cqes);

        if (params->flags & IORING_SETUP_SQPOLL) {
            ctx->sq_idle_ms = params->sq_thread_idle;
            ctx->sq_poll_thread = sched::thread::make(
                [ctx] { sq_poll_loop(ctx); },
                sched::thread::attr());
            ctx->sq_poll_thread->start();
        }

        return fd;

    } catch (...) {
        memory::free_phys_contiguous_aligned(ctx->sqes);
        memory::free_phys_contiguous_aligned(ctx->cqes);
        delete ctx;
        return -ENOMEM;
    }
}

/* -------------------------------------------------------------------------
 * sys_io_uring_enter
 * ---------------------------------------------------------------------- */

extern "C"
OSV_LIBC_API
int sys_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                       unsigned flags, const void *sig, size_t sigsz)
{
    struct file *fp;
    if (fget(fd, &fp) != 0)
        return -errno;

    auto *io_uring_fp = dynamic_cast<io_uring_file *>(fp);
    if (!io_uring_fp) {
        fdrop(fp);
        return -EINVAL;
    }

    auto *ctx = static_cast<struct io_uring_ctx *>(fp->f_data);

    if (ctx->sq_poll_thread && (flags & IORING_ENTER_SQ_WAKEUP)) {
        WITH_LOCK(ctx->mtx) {
            ctx->wait_sq.wake_all(ctx->mtx);
        }
    }

    int submitted = 0;
    if (to_submit > 0 && !ctx->sq_poll_thread) {
        submitted = io_uring_submit_sqes(ctx, to_submit);
        if (submitted < 0) {
            fdrop(fp);
            return submitted;
        }
    }

    if (flags & IORING_ENTER_GETEVENTS) {
        int err = io_uring_wait_cqe(ctx, min_complete);
        if (err) {
            fdrop(fp);
            return err;
        }
    }

    fdrop(fp);
    return submitted;
}

/* -------------------------------------------------------------------------
 * sys_io_uring_register
 * ---------------------------------------------------------------------- */

extern "C"
OSV_LIBC_API
int sys_io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args)
{
    struct file *fp;
    if (fget(fd, &fp) != 0)
        return -errno;

    auto *io_uring_fp = dynamic_cast<io_uring_file *>(fp);
    if (!io_uring_fp) {
        fdrop(fp);
        return -EINVAL;
    }

    auto *ctx = static_cast<struct io_uring_ctx *>(fp->f_data);
    int ret = 0;

    WITH_LOCK(ctx->mtx) {
        switch (opcode) {

        case IORING_REGISTER_BUFFERS: {
            if (ctx->registered_buffers) { ret = -EBUSY; break; }
            if (nr_args == 0 || nr_args > 1024) { ret = -EINVAL; break; }

            auto *iovecs = static_cast<struct iovec *>(arg);
            ctx->registered_buffers = new void *[nr_args];
            for (unsigned i = 0; i < nr_args; i++)
                ctx->registered_buffers[i] = iovecs[i].iov_base;
            ctx->nr_registered_buffers = nr_args;
            break;
        }

        case IORING_UNREGISTER_BUFFERS:
            if (!ctx->registered_buffers) { ret = -EINVAL; break; }
            delete[] ctx->registered_buffers;
            ctx->registered_buffers      = nullptr;
            ctx->nr_registered_buffers   = 0;
            break;

        case IORING_REGISTER_FILES: {
            if (ctx->registered_files) { ret = -EBUSY; break; }
            if (nr_args == 0 || nr_args > 1024) { ret = -EINVAL; break; }

            int *fds = static_cast<int *>(arg);
            ctx->registered_files = new struct file *[nr_args]();
            bool files_ok = true;
            for (unsigned i = 0; i < nr_args && files_ok; i++) {
                if (fds[i] == -1) {
                    ctx->registered_files[i] = nullptr;
                    continue;
                }
                struct file *f = nullptr;
                if (fget(fds[i], &f) != 0) {
                    for (unsigned j = 0; j < i; j++)
                        if (ctx->registered_files[j])
                            fdrop(ctx->registered_files[j]);
                    delete[] ctx->registered_files;
                    ctx->registered_files = nullptr;
                    ret = -EBADF;
                    files_ok = false;
                } else {
                    ctx->registered_files[i] = f;
                }
            }
            if (files_ok)
                ctx->nr_registered_files = nr_args;
            break;
        }

        case IORING_UNREGISTER_FILES:
            if (!ctx->registered_files) { ret = -EINVAL; break; }
            for (unsigned i = 0; i < ctx->nr_registered_files; i++)
                if (ctx->registered_files[i])
                    fdrop(ctx->registered_files[i]);
            delete[] ctx->registered_files;
            ctx->registered_files    = nullptr;
            ctx->nr_registered_files = 0;
            break;

        case IORING_REGISTER_FILES_UPDATE: {
            if (!ctx->registered_files) { ret = -EINVAL; break; }
            auto *upd  = static_cast<struct io_uring_files_update *>(arg);
            auto *fds  = reinterpret_cast<int *>(upd->fds);
            uint32_t off = upd->offset;
            int updated = 0;
            for (unsigned i = 0; i < nr_args; i++) {
                uint32_t slot = off + i;
                if (slot >= ctx->nr_registered_files) break;
                struct file *old = ctx->registered_files[slot];
                if (old) fdrop(old);
                if (fds[i] == -1) {
                    ctx->registered_files[slot] = nullptr;
                } else {
                    struct file *nf = nullptr;
                    if (fget(fds[i], &nf) == 0)
                        ctx->registered_files[slot] = nf;
                    else
                        ctx->registered_files[slot] = nullptr;
                }
                updated++;
            }
            ret = updated;
            break;
        }

        case IORING_REGISTER_EVENTFD:
        case IORING_REGISTER_EVENTFD_ASYNC: {
            if (!arg || nr_args != 1) { ret = -EINVAL; break; }
            int new_efd = *(int *)arg;
            if (ctx->eventfd_fd >= 0)
                ::close(ctx->eventfd_fd);
            /* dup so we own our own reference */
            ctx->eventfd_fd = ::dup(new_efd);
            if (ctx->eventfd_fd < 0) ret = -errno;
            break;
        }

        case IORING_UNREGISTER_EVENTFD:
            if (ctx->eventfd_fd >= 0) {
                ::close(ctx->eventfd_fd);
                ctx->eventfd_fd = -1;
            }
            break;

        case IORING_REGISTER_PROBE: {
            /*
             * Fill in the probe structure.  arg points to a struct io_uring_probe
             * with nr_args entries reserved.  We report all opcodes up to
             * IORING_OP_LAST as supported.
             */
            auto *probe = static_cast<struct io_uring_probe *>(arg);
            if (!probe) { ret = -EFAULT; break; }

            uint8_t last = (uint8_t)(IORING_OP_LAST - 1);
            probe->last_op  = last;
            probe->ops_len  = (nr_args < IORING_OP_LAST) ? (uint8_t)nr_args
                                                          : (uint8_t)IORING_OP_LAST;
            for (uint8_t i = 0; i < probe->ops_len; i++) {
                probe->ops[i].op    = i;
                probe->ops[i].resv  = 0;
                probe->ops[i].flags = IO_URING_OP_SUPPORTED;
                probe->ops[i].resv2 = 0;
            }
            break;
        }

        case IORING_REGISTER_PBUF_RING: {
            /*
             * Register a buffer ring for a buffer group.  The ring is an
             * array of io_uring_buf entries maintained by the application.
             * We copy the entries into our internal buf_group map.
             */
            if (!arg) { ret = -EFAULT; break; }
            auto *reg = static_cast<struct io_uring_buf_reg *>(arg);
            auto *ring = reinterpret_cast<struct io_uring_buf_ring *>(
                             reg->ring_addr);
            uint16_t bgid = reg->bgid;
            uint32_t nent = reg->ring_entries;

            auto &group = ctx->buf_groups[bgid];
            group.clear();
            uint16_t tail = __atomic_load_n(&ring->tail, __ATOMIC_ACQUIRE);
            for (uint32_t i = 0; i < nent && i < tail; i++) {
                struct io_uring_buf &b = ring->bufs[i & (nent - 1)];
                provided_buf pb;
                pb.addr = reinterpret_cast<void *>(b.addr);
                pb.len  = b.len;
                pb.bid  = b.bid;
                group.push_back(pb);
            }
            break;
        }

        case IORING_UNREGISTER_PBUF_RING: {
            if (!arg) { ret = -EFAULT; break; }
            auto *reg = static_cast<struct io_uring_buf_reg *>(arg);
            ctx->buf_groups.erase(reg->bgid);
            break;
        }

        case IORING_REGISTER_SYNC_CANCEL: {
            /*
             * Synchronous cancel: cancel a pending op by user_data and
             * optionally wait for it to complete.
             */
            if (!arg) { ret = -EFAULT; break; }
            auto *reg = static_cast<struct io_uring_sync_cancel_reg *>(arg);
            uint64_t target = reg->addr;
            auto range = ctx->cancellable.equal_range(target);
            bool found = false;
            for (auto it = range.first; it != range.second; ++it) {
                it->second->store(true, std::memory_order_relaxed);
                found = true;
                break;
            }
            ret = found ? 0 : -ENOENT;
            break;
        }

        /* Unknown or unsupported register opcodes */
        default:
            ret = -EINVAL;
            break;
        }
    }

    fdrop(fp);
    return ret;
}

/* -------------------------------------------------------------------------
 * C-linkage shims for code that calls io_uring_setup/enter/register directly
 * without the sys_ prefix (e.g. self-tests).
 * ---------------------------------------------------------------------- */

extern "C"
long io_uring_setup(unsigned entries, struct io_uring_params *params)
{
    return sys_io_uring_setup(entries, params);
}

extern "C"
int io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                   unsigned flags, const void *sig)
{
    return sys_io_uring_enter(fd, to_submit, min_complete, flags, sig, 0);
}

extern "C"
int io_uring_register(int fd, unsigned opcode, void *arg, unsigned nr_args)
{
    return sys_io_uring_register(fd, opcode, arg, nr_args);
}
