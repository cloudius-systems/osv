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
#include <osv/condvar.h>
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
#include <algorithm>

/* Forward declaration */
class io_uring_file;
struct cancel_token;
struct io_uring_work;

/*
 * Return the io_uring_ctx backing a file, or nullptr if `fp` is not an
 * io_uring fd.  Defined after io_uring_file (below) because the RTTI check
 * needs the complete type; forward-declared here so exec_single_sqe (which
 * runs MSG_RING against a *target* ring fd) can use it.
 */
static struct io_uring_ctx *io_uring_ctx_from_file(struct file *fp);

/*
 * OSv's futex() (defined in linux.cc, C++ linkage, global namespace).  Only
 * FUTEX_WAIT / FUTEX_WAKE / FUTEX_WAIT_BITSET(match-any) are implemented; used
 * by IORING_OP_FUTEX_WAIT / IORING_OP_FUTEX_WAKE.
 */
#ifndef FUTEX_WAIT
#define FUTEX_WAIT 0
#endif
#ifndef FUTEX_WAKE
#define FUTEX_WAKE 1
#endif
int futex(int *uaddr, int op, int val, const struct timespec *timeout,
          int *uaddr2, uint32_t val3);

/* Round up to the next power of two, leaving 0 and 1 unchanged.  A plain
 * clz-based round-up turns 1 into 2 (it feeds 1 into __builtin_clz), which
 * would silently inflate a requested queue depth of 1. */
static inline uint32_t round_up_pow2(uint32_t v)
{
    if (v <= 1)
        return v;
    return 1U << (32 - __builtin_clz(v - 1));
}

/* A single provided buffer entry (from IORING_OP_PROVIDE_BUFFERS) */
struct provided_buf {
    void    *addr;
    uint32_t len;
    uint16_t bid;
};

/*
 * A registered provided-buffer ring (IORING_REGISTER_PBUF_RING).  Unlike the
 * classic PROVIDE_BUFFERS deque, this is a LIVE shared ring: the application
 * writes io_uring_buf entries and advances ring->tail; the kernel side picks
 * bufs[head & mask] and advances its own head.  We store the ring pointer and
 * mask and read ring->tail on every pick so newly-added buffers are seen
 * without re-registration.  Protected by ctx->mtx.
 */
struct buf_ring {
    struct io_uring_buf_ring *ring = nullptr;
    uint32_t mask   = 0;   /* ring_entries - 1 */
    uint16_t head   = 0;   /* kernel-side consumed counter */
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
    size_t         *registered_buffer_lens;
    unsigned        nr_registered_buffers;
    struct file   **registered_files;
    unsigned        nr_registered_files;

    /* Setup-time flags (IORING_SETUP_*) persisted from io_uring_params.flags */
    uint32_t setup_flags;

    /*
     * IORING_SETUP_R_DISABLED: the ring starts disabled and rejects
     * submissions until IORING_REGISTER_ENABLE_RINGS clears this.
     */
    bool disabled;

    /* SQPOLL */
    sched::thread *sq_poll_thread;
    uint32_t       sq_idle_ms;
    uint32_t       sq_thread_cpu;   /* IORING_SETUP_SQ_AFF target CPU */

    /* Eventfd for completion notification */
    int eventfd_fd;

    /*
     * Cancellable pending operations keyed by user_data.
     * Each entry is an atomic<bool> shared with the work thread.
     * Protected by mtx.
     */
    std::unordered_multimap<uint64_t, cancel_token*> cancellable;

    /*
     * Provided buffer groups: buf_group_id -> deque of available buffers.
     * Protected by mtx.
     */
    std::unordered_map<uint16_t, std::deque<provided_buf>> buf_groups;

    /*
     * Registered live buffer rings (IORING_REGISTER_PBUF_RING):
     * buf_group_id -> live ring.  Takes precedence over buf_groups for a
     * given bgid.  Protected by mtx.
     */
    std::unordered_map<uint16_t, buf_ring> buf_rings;

    /*
     * CQ overflow backlog.  When the visible CQ ring is full, completions are
     * queued here instead of being dropped (IORING_FEAT_NODROP honesty) and
     * flushed into the ring as the application consumes CQEs.  Protected by
     * mtx.
     */
    std::deque<struct io_uring_cqe> cq_overflow;

    /*
     * io-wq worker pool.  Replaces the former detached-thread-per-chain model
     * (which created one sched::thread per submitted SQE chain -- a thread-
     * creation amplifier under PostgreSQL-style batch submit).  Persistent
     * workers pull chains from per-class queues and are reused across
     * submissions, so thread creation is bounded to wq_max_workers per class
     * for the whole lifetime of the ring rather than growing with load.
     *
     * Two classes mirror Linux io-wq and IORING_REGISTER_IOWQ_MAX_WORKERS,
     * which takes a [bounded, unbounded] pair:
     *   index 0 = WQ_BOUNDED   -- file/disk ops that complete promptly.
     *   index 1 = WQ_UNBOUNDED -- net/poll/timeout ops that may block
     *                             indefinitely; kept in a separate pool so a
     *                             burst of blocking waits cannot starve disk
     *                             I/O of workers.
     * All fields protected by mtx.
     */
    std::deque<std::shared_ptr<std::vector<io_uring_work>>> wq_queue[2];
    waitqueue   wq_wait[2];
    uint32_t    wq_nr_workers[2];   /* threads created per class */
    uint32_t    wq_idle[2];         /* workers currently blocked on wq_wait */
    uint32_t    wq_max_workers[2];  /* cap per class (IOWQ_MAX_WORKERS) */
    std::vector<sched::thread*> wq_threads;  /* all workers, joined at teardown */

    io_uring_ctx()
        : pending_ops(0), shutdown(false),
          registered_buffers(nullptr), registered_buffer_lens(nullptr),
          nr_registered_buffers(0),
          registered_files(nullptr), nr_registered_files(0),
          setup_flags(0), disabled(false),
          sq_poll_thread(nullptr), sq_idle_ms(0), sq_thread_cpu(0),
          eventfd_fd(-1)
    {
        unsigned ncpu = (unsigned)sched::cpus.size();
        if (ncpu == 0) ncpu = 1;
        wq_max_workers[0] = std::max(4u, ncpu);       /* bounded (disk) */
        wq_max_workers[1] = std::max(16u, ncpu * 4);  /* unbounded (net) */
        wq_nr_workers[0] = wq_nr_workers[1] = 0;
        wq_idle[0] = wq_idle[1] = 0;
    }
};

/*
 * Cancellation token for one work item.  Shared (by pointer) between the
 * work item and the ctx->cancellable map so that a cancel request can reach
 * an operation whether it is still queued or already in flight.
 *
 *   cancelled  - set by a cancel request; checked before the op starts.
 *   worker     - the thread running this op once it is in flight, else null.
 *                A cancel request that finds a non-null worker interrupts it,
 *                unblocking an interruptible wait (socket recv/accept/etc.)
 *                with EINTR so the op returns instead of hanging forever.
 */
struct cancel_token {
    std::atomic<bool>           cancelled{false};
    std::atomic<sched::thread*> worker{nullptr};
};

/*
 * Unit of async work.  Each linked chain is executed as one unit in a single
 * thread; a lone (unlinked) SQE is a chain of length 1.
 */
struct io_uring_work {
    struct io_uring_ctx *ctx;
    struct io_uring_sqe  sqe;
    cancel_token         cancel;

    io_uring_work() = default;
    /* cancel_token is non-movable; define a move ctor that copies its state */
    io_uring_work(io_uring_work&& o) noexcept
        : ctx(o.ctx), sqe(o.sqe)
    {
        cancel.cancelled.store(o.cancel.cancelled.load(std::memory_order_relaxed),
                               std::memory_order_relaxed);
        cancel.worker.store(o.cancel.worker.load(std::memory_order_relaxed),
                            std::memory_order_relaxed);
    }
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

/*
 * Raise or clear IORING_SQ_CQ_OVERFLOW in the SQ ring flags so the
 * application can observe that a backlog exists (and must drain the CQ /
 * re-enter to flush it).  ctx->mtx must be held.
 */
static void set_cq_overflow_flag_locked(struct io_uring_ctx *ctx, bool on)
{
    if (!ctx->sq_ring)
        return;
    auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
    if (on)
        sq->flags.fetch_or(IORING_SQ_CQ_OVERFLOW, std::memory_order_release);
    else
        sq->flags.fetch_and(~IORING_SQ_CQ_OVERFLOW, std::memory_order_release);
}

/*
 * Move backlogged completions into the visible CQ ring while free slots
 * exist, preserving FIFO order.  Clears the overflow flag once drained.
 * ctx->mtx must be held.  Does not touch pending_ops (those were already
 * accounted when the completion was first produced).
 */
static void flush_cq_overflow_locked(struct io_uring_ctx *ctx)
{
    if (ctx->cq_overflow.empty())
        return;

    while (!ctx->cq_overflow.empty()) {
        uint32_t head, tail;
        if (ctx->cq_ring) {
            auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
            head = cq->head.load(std::memory_order_acquire);
            tail = cq->tail.load(std::memory_order_acquire);
        } else {
            head = ctx->cq.head;
            tail = ctx->cq.tail;
        }

        uint32_t next = tail + 1;
        if ((next - head) > ctx->cq.entries)
            break;      /* ring still full; leave the rest backlogged */

        const struct io_uring_cqe &c = ctx->cq_overflow.front();
        struct io_uring_cqe *slot = &ctx->cqes[tail & ctx->cq.mask];
        slot->user_data = c.user_data;
        slot->res       = c.res;
        slot->flags     = c.flags;
        ctx->cq_overflow.pop_front();

        std::atomic_thread_fence(std::memory_order_release);

        if (ctx->cq_ring) {
            auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
            cq->tail.store(next, std::memory_order_release);
        }
        ctx->cq.tail = next;
    }

    if (ctx->cq_overflow.empty())
        set_cq_overflow_flag_locked(ctx, false);
}

/*
 * Post a single CQE to the ring (or the overflow backlog).  ctx->mtx must be
 * held.  Does NOT touch pending_ops or wake waiters -- the caller decides
 * whether this completion retires an op.  Shared by io_uring_complete_op()
 * (terminal completions) and the multishot intermediate poster.
 */
static void io_uring_post_cqe_locked(struct io_uring_ctx *ctx,
                                     uint64_t user_data,
                                     int32_t  res,
                                     uint32_t cqe_flags)
{
    /* Drain any backlogged CQEs into slots the app has freed. */
    flush_cq_overflow_locked(ctx);

    uint32_t head, tail, next;

    if (ctx->cq_ring) {
        auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
        head = cq->head.load(std::memory_order_acquire);
        tail = cq->tail.load(std::memory_order_acquire);
    } else {
        head = ctx->cq.head;
        tail = ctx->cq.tail;
    }

    next = tail + 1;
    if (!ctx->cq_overflow.empty() || (next - head) > ctx->cq.entries) {
        /*
         * CQ ring is full (or a backlog already exists and ordering must
         * be preserved): enqueue to the overflow backlog instead of
         * dropping, and raise the overflow flag.  IORING_FEAT_NODROP
         * guarantees no completion is lost.
         */
        struct io_uring_cqe c;
        c.user_data = user_data;
        c.res       = res;
        c.flags     = cqe_flags;
        ctx->cq_overflow.push_back(c);
        set_cq_overflow_flag_locked(ctx, true);
    } else {
        struct io_uring_cqe *cqe = &ctx->cqes[tail & ctx->cq.mask];
        cqe->user_data = user_data;
        cqe->res       = res;
        cqe->flags     = cqe_flags;

        std::atomic_thread_fence(std::memory_order_release);

        if (ctx->cq_ring) {
            auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
            cq->tail.store(next, std::memory_order_release);
        }
        ctx->cq.tail = next;
    }
}

static void io_uring_complete_op(struct io_uring_ctx *ctx,
                                 uint64_t user_data,
                                 int32_t  res,
                                 uint32_t cqe_flags)
{
    WITH_LOCK(ctx->mtx) {
        io_uring_post_cqe_locked(ctx, user_data, res, cqe_flags);
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

/*
 * Post a multishot intermediate CQE with IORING_CQE_F_MORE set.  The op stays
 * armed (pending_ops is NOT decremented), so only the terminal CQE -- posted
 * later via io_uring_complete_op() without F_MORE -- retires it.
 */
static void io_uring_post_multishot_cqe(struct io_uring_ctx *ctx,
                                        uint64_t user_data,
                                        int32_t  res,
                                        uint32_t cqe_flags)
{
    WITH_LOCK(ctx->mtx) {
        io_uring_post_cqe_locked(ctx, user_data, res,
                                 cqe_flags | IORING_CQE_F_MORE);
        ctx->wait_cq.wake_all(ctx->mtx);
    }

    if (ctx->eventfd_fd >= 0) {
        uint64_t val = 1;
        (void)::write(ctx->eventfd_fd, &val, sizeof(val));
    }
}

/* -------------------------------------------------------------------------
 * Cancel a single tracked op by its cancel_token.  Must be called with
 * ctx->mtx held.  Sets the cancelled flag (so a not-yet-started op will be
 * short-circuited) and, if the op is already in flight on a worker thread,
 * interrupts that thread so an interruptible blocking wait returns EINTR.
 * ---------------------------------------------------------------------- */

static void cancel_token_fire(cancel_token *tok)
{
    tok->cancelled.store(true, std::memory_order_relaxed);
    sched::thread *w = tok->worker.load(std::memory_order_acquire);
    if (w)
        w->interrupted(true);
}

/* -------------------------------------------------------------------------
 * Convert an io_uring timeout spec (__kernel_timespec + timeout_flags) into
 * an absolute monotonic (uptime) deadline suitable for condvar/timer waits.
 *
 * Honors IORING_TIMEOUT_ABS and the clock-selection flags:
 *   - relative (default): now() + (sec, nsec)
 *   - ABS + BOOTTIME:      absolute uptime nanoseconds
 *   - ABS + REALTIME/def:  absolute wall-clock ns since epoch, mapped to
 *                          uptime by subtracting the current boot_time().
 * ---------------------------------------------------------------------- */

static osv::clock::uptime::time_point
io_uring_deadline(const struct __kernel_timespec *ts, uint32_t flags)
{
    using namespace std::chrono;
    auto dur = duration_cast<osv::clock::uptime::duration>(
                   seconds(ts->tv_sec) + nanoseconds(ts->tv_nsec));

    if (flags & IORING_TIMEOUT_ABS) {
        if (flags & IORING_TIMEOUT_BOOTTIME) {
            return osv::clock::uptime::time_point(dur);
        }
        /* Absolute wall-clock target -> uptime via boot_time offset. */
        auto wall_target = osv::clock::wall::time_point(
            duration_cast<osv::clock::wall::duration>(dur));
        auto rel = wall_target - osv::clock::wall::boot_time();
        return osv::clock::uptime::time_point(
            duration_cast<osv::clock::uptime::duration>(rel));
    }
    return osv::clock::uptime::now() + dur;
}


/* -------------------------------------------------------------------------
 * Pick a provided buffer for buffer-select (IOSQE_BUFFER_SELECT).  Tries the
 * live registered buffer ring (IORING_REGISTER_PBUF_RING) first, reading its
 * tail fresh so buffers the app added since registration are visible; falls
 * back to the classic PROVIDE_BUFFERS deque.  ctx->mtx MUST be held.
 * Returns true and fills *out on success; false if no buffer is available.
 * ---------------------------------------------------------------------- */
static bool io_uring_pick_buffer(struct io_uring_ctx *ctx, uint16_t bgid,
                                 provided_buf *out)
{
    auto rit = ctx->buf_rings.find(bgid);
    if (rit != ctx->buf_rings.end() && rit->second.ring) {
        buf_ring &br = rit->second;
        uint16_t tail = __atomic_load_n(&br.ring->tail, __ATOMIC_ACQUIRE);
        if ((uint16_t)(tail - br.head) != 0) {
            struct io_uring_buf &b = br.ring->bufs[br.head & br.mask];
            out->addr = reinterpret_cast<void *>(b.addr);
            out->len  = b.len;
            out->bid  = b.bid;
            br.head++;
            return true;
        }
        return false;   /* live ring registered but empty: do not fall back */
    }

    auto git = ctx->buf_groups.find(bgid);
    if (git != ctx->buf_groups.end() && !git->second.empty()) {
        *out = git->second.front();
        git->second.pop_front();
        return true;
    }
    return false;
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
            uintptr_t base = reinterpret_cast<uintptr_t>(ctx->registered_buffers[idx]);
            size_t    blen = ctx->registered_buffer_lens[idx];
            uintptr_t want = sqe->addr;
            if (want < base || sqe->len > blen ||
                want + sqe->len > base + blen) {
                if (!fixed) fdrop(fp);
                res = -EFAULT;
                break;
            }
            iov_local.iov_base = reinterpret_cast<void *>(want);
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
            uintptr_t base = reinterpret_cast<uintptr_t>(ctx->registered_buffers[idx]);
            size_t    blen = ctx->registered_buffer_lens[idx];
            uintptr_t want = sqe->addr;
            if (want < base || sqe->len > blen ||
                want + sqe->len > base + blen) {
                if (!fixed) fdrop(fp);
                res = -EFAULT;
                break;
            }
            iov_local.iov_base = reinterpret_cast<void *>(want);
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
        /*
         * Linux sync_file_range(2) semantics on the SQE:
         *   off  = starting offset, len = nbytes (0 nbytes = "to EOF"),
         *   sync_range_flags = SYNC_FILE_RANGE_{WAIT_BEFORE,WRITE,WAIT_AFTER}.
         * It validates its arguments strictly, rejecting a negative offset,
         * negative/overflowing length, or unknown flag bits with -EINVAL.
         *
         * OSv has no ranged writeback primitive, so once the arguments are
         * validated we issue a full fsync(): flushing the whole file is a
         * correct superset of flushing a byte range, so the durability the
         * caller asked for is honored (never under-synced).  We validate
         * first rather than silently ignoring bad input.
         */
        int64_t off   = (int64_t)sqe->off;
        int64_t nbytes = (int64_t)sqe->len;
        uint32_t sflags = sqe->sync_range_flags;
        const uint32_t valid = SYNC_FILE_RANGE_WAIT_BEFORE |
                               SYNC_FILE_RANGE_WRITE |
                               SYNC_FILE_RANGE_WAIT_AFTER;
        if ((sflags & ~valid) || off < 0 || nbytes < 0 ||
            (nbytes && off > INT64_MAX - nbytes)) {
            res = -EINVAL;
            break;
        }
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
        /*
         * OSv's ::poll() (core/poll.cc poll_many) blocks in a
         * non-interruptible wait_until, so a single long poll cannot be
         * broken by a cancel.  Instead poll in short slices and check this
         * worker's interrupt flag between slices: a POLL_REMOVE / ASYNC_CANCEL
         * fires cancel_token_fire() which sets interrupted(true), so we notice
         * within one slice and return -ECANCELED.  The wait is otherwise
         * indefinite (one-shot readiness), matching Linux POLL_ADD: a plain
         * poll() timeout (r == 0) is not a completion.
         */
        res = -ECANCELED;
        while (!sched::thread::current()->interrupted() && !ctx->shutdown) {
            int r = ::poll(&pfd, 1, 100 /* ms slice */);
            if (r < 0)      { res = -errno; break; }
            if (r > 0)      { res = pfd.revents; break; }
            /* r == 0: slice timed out with no events -- loop and re-check */
        }
        break;
    }

    /* --- POLL_REMOVE --- */
    case IORING_OP_POLL_REMOVE: {
        /*
         * Cancel a pending POLL_ADD identified by sqe->addr == original
         * user_data.  Fire its cancel token; the sliced-poll loop notices the
         * interrupt flag and returns -ECANCELED.  Mirrors TIMEOUT_REMOVE /
         * ASYNC_CANCEL.  Returns 0 if a match was found, else -ENOENT.
         */
        uint64_t target = sqe->addr;
        bool found = false;
        WITH_LOCK(ctx->mtx) {
            auto range = ctx->cancellable.equal_range(target);
            for (auto it = range.first; it != range.second; ++it) {
                cancel_token_fire(it->second);
                found = true;
                break;
            }
        }
        res = found ? 0 : -ENOENT;
        break;
    }

    /* --- TIMEOUT --- */
    case IORING_OP_TIMEOUT: {
        /*
         * This implementation treats the timespec as a relative duration and
         * honors only IORING_TIMEOUT_ETIME_SUCCESS.  Absolute/clock-select and
         * multishot variants are not implemented, so reject them rather than
         * silently mistreating an absolute deadline as a relative sleep.
         */
        const uint32_t supported_timeout_flags = IORING_TIMEOUT_ETIME_SUCCESS;
        if (sqe->timeout_flags & ~supported_timeout_flags) {
            res = -EINVAL;
            break;
        }
        auto *ts = reinterpret_cast<const struct __kernel_timespec *>(sqe->addr);
        if (!ts) { res = -EFAULT; break; }
        if (ts->tv_sec < 0 || ts->tv_nsec < 0 || ts->tv_nsec >= 1000000000L) {
            res = -EINVAL;
            break;
        }
        auto deadline = io_uring_deadline(ts, sqe->timeout_flags);

        uint32_t count = sqe->len;      /* completion-count trigger */

        /*
         * Arm a real timer to the (possibly absolute) deadline and block on
         * the CQ waitqueue, which is woken by every completion.  This replaces
         * the former 1 ms busy-poll: we sleep until either the timer fires or
         * enough completions have arrived.  IORING_TIMEOUT_ABS and the clock
         * flags are honored by io_uring_deadline().
         */
        sched::timer tmr(*sched::thread::current());
        tmr.set(deadline);

        bool timed_out;
        WITH_LOCK(ctx->mtx) {
            uint32_t start_tail = ctx->cq_ring
                ? static_cast<struct io_uring_cq_ring *>(ctx->cq_ring)
                      ->tail.load(std::memory_order_acquire)
                : ctx->cq.tail;

            for (;;) {
                if (count > 0) {
                    uint32_t cur_tail = ctx->cq_ring
                        ? static_cast<struct io_uring_cq_ring *>(ctx->cq_ring)
                              ->tail.load(std::memory_order_acquire)
                        : ctx->cq.tail;
                    if ((cur_tail - start_tail) >= count) {
                        timed_out = false;
                        break;
                    }
                }
                if (tmr.expired()) {
                    timed_out = true;
                    break;
                }
                /* Blocks until a completion wakes wait_cq or the timer fires. */
                sched::thread::wait_for(ctx->mtx, ctx->wait_cq, tmr);
            }
        }
        tmr.cancel();

        res = timed_out ? -ETIME : 0;
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
                cancel_token_fire(it->second);
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
                cancel_token_fire(it->second);
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
                found = io_uring_pick_buffer(ctx, bgid, &pb);
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
         * OSv has no pipes and no zero-copy splice, so this is an honest
         * bounce-buffer copy through the VFS file layer (sys_read/sys_write),
         * not the raw ::read/::pread of unresolved integer fds.
         *
         * SQE layout (Linux):
         *   splice_fd_in    = source fd (registered-file index if
         *                     splice_flags & SPLICE_F_FD_IN_FIXED)
         *   splice_off_in   = source offset, or (uint64_t)-1 for "current pos"
         *   fd              = destination fd (registered if IOSQE_FIXED_FILE)
         *   off             = destination offset, or (uint64_t)-1 for cur pos
         *   len             = bytes to move
         *
         * NOTE: SPLICE_F_FD_IN_FIXED selects a REGISTERED source file; it does
         * NOT mean "use current position" (the previous implementation had this
         * backwards and drove ::pread on a raw fd).
         */
        size_t total = sqe->len;

        bool in_fixed = (sqe->splice_flags & SPLICE_F_FD_IN_FIXED) != 0;
        struct file *fin = in_fixed
            ? (((unsigned)sqe->splice_fd_in < ctx->nr_registered_files)
                   ? ctx->registered_files[sqe->splice_fd_in] : nullptr)
            : nullptr;
        if (!in_fixed && fget(sqe->splice_fd_in, &fin) != 0)
            fin = nullptr;
        if (!fin) { res = -EBADF; break; }

        bool out_fixed;
        struct file *fout = resolve_fd(ctx, sqe->fd, sqe->flags, out_fixed);
        if (!fout) {
            if (!in_fixed) fdrop(fin);
            res = -EBADF;
            break;
        }

        off_t off_in  = (sqe->splice_off_in == (uint64_t)-1) ? -1
                        : (off_t)sqe->splice_off_in;
        off_t off_out = (sqe->off == (uint64_t)-1) ? -1 : (off_t)sqe->off;

        size_t chunk  = (total < 65536) ? total : 65536;
        char  *bounce = new char[chunk];
        size_t copied = 0;
        int    err    = 0;

        while (copied < total) {
            size_t want = total - copied;
            if (want > chunk) want = chunk;

            struct iovec riov = { bounce, want };
            size_t got = 0;
            int r = sys_read(fin, &riov, 1, off_in, &got);
            if (r != 0) { err = r; break; }
            if (got == 0) break;          /* EOF */
            if (off_in >= 0) off_in += (off_t)got;

            size_t wr = 0;
            while (wr < got) {
                struct iovec wiov = { bounce + wr, got - wr };
                size_t put = 0;
                int w = sys_write(fout, &wiov, 1, off_out, &put);
                if (w != 0) { err = w; break; }
                if (put == 0) { err = EIO; break; }
                if (off_out >= 0) off_out += (off_t)put;
                wr     += put;
                copied += put;
            }
            if (err) break;
        }
        delete[] bounce;
        if (!in_fixed)  fdrop(fin);
        if (!out_fixed) fdrop(fout);
        res = err ? -err : (int32_t)copied;
        break;
    }

    /* --- TEE --- */
    case IORING_OP_TEE: {
        /*
         * tee(2) duplicates data between two pipes WITHOUT consuming the
         * source.  OSv has no pipes, so a faithful tee is impossible.  We
         * emulate it for seekable source files only: read a chunk, restore the
         * source position with sys_lseek, then write to the destination.  A
         * non-seekable source (socket/eventfd/tty) makes non-consuming reads
         * impossible, so we honestly return -ESPIPE.
         *
         * fd_in is registered iff SPLICE_F_FD_IN_FIXED; fd_out iff
         * IOSQE_FIXED_FILE.  No explicit offsets (tee has none) — always
         * current position, restored on the source after each read.
         */
        size_t total = sqe->len;

        bool in_fixed = (sqe->splice_flags & SPLICE_F_FD_IN_FIXED) != 0;
        struct file *fin = in_fixed
            ? (((unsigned)sqe->splice_fd_in < ctx->nr_registered_files)
                   ? ctx->registered_files[sqe->splice_fd_in] : nullptr)
            : nullptr;
        if (!in_fixed && fget(sqe->splice_fd_in, &fin) != 0)
            fin = nullptr;
        if (!fin) { res = -EBADF; break; }

        bool out_fixed;
        struct file *fout = resolve_fd(ctx, sqe->fd, sqe->flags, out_fixed);
        if (!fout) {
            if (!in_fixed) fdrop(fin);
            res = -EBADF;
            break;
        }

        /* Source must be seekable for non-consuming reads. */
        off_t pos = 0;
        if (sys_lseek(fin, 0, SEEK_CUR, &pos) != 0) {
            if (!in_fixed)  fdrop(fin);
            if (!out_fixed) fdrop(fout);
            res = -ESPIPE;
            break;
        }

        size_t chunk  = (total < 65536) ? total : 65536;
        char  *bounce = new char[chunk];
        size_t copied = 0;
        int    err    = 0;

        while (copied < total) {
            size_t want = total - copied;
            if (want > chunk) want = chunk;

            struct iovec riov = { bounce, want };
            size_t got = 0;
            int r = sys_read(fin, &riov, 1, pos, &got);
            if (r != 0) { err = r; break; }
            if (got == 0) break;          /* EOF — source not advanced */

            size_t wr = 0;
            while (wr < got) {
                struct iovec wiov = { bounce + wr, got - wr };
                size_t put = 0;
                int w = sys_write(fout, &wiov, 1, -1, &put);
                if (w != 0) { err = w; break; }
                if (put == 0) { err = EIO; break; }
                wr     += put;
                copied += put;
            }
            if (err) break;
            /* tee does not consume the source: leave pos where it was. */
        }
        delete[] bounce;
        if (!in_fixed)  fdrop(fin);
        if (!out_fixed) fdrop(fout);
        res = err ? -err : (int32_t)copied;
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
        /*
         * The Linux ABI for this opcode uses renameat2() semantics, carrying
         * flags in sqe->rename_flags.  OSv only implements plain renameat(),
         * so reject any nonzero flags rather than silently dropping them.
         */
        if (sqe->rename_flags) { res = -EINVAL; break; }
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

    /* --- SOCKET --- */
    case IORING_OP_SOCKET: {
        /* fd=domain, off=type, len=protocol, splice_fd_in=file_index.
         * OSv has no "install into registered-file slot" path for a freshly
         * created fd, so the fixed-file variant is rejected honestly. */
        if (sqe->splice_fd_in != 0) { res = -EINVAL; break; }
        int domain   = sqe->fd;
        int type     = (int)sqe->off;
        int protocol = (int)sqe->len;
        int r = ::socket(domain, type, protocol);
        res = (r < 0) ? -errno : r;
        break;
    }

    /* --- BIND --- */
    case IORING_OP_BIND: {
        /* fd=sockfd, addr=sockaddr ptr, addr2=addr_len */
        auto *addr = reinterpret_cast<const struct sockaddr *>(sqe->addr);
        socklen_t len = (socklen_t)sqe->addr2;
        int r = ::bind(sqe->fd, addr, len);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- LISTEN --- */
    case IORING_OP_LISTEN: {
        /* fd=sockfd, len=backlog */
        int r = ::listen(sqe->fd, (int)sqe->len);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- FTRUNCATE --- */
    case IORING_OP_FTRUNCATE: {
        /* fd=fd, off=length */
        int r = ::ftruncate(sqe->fd, (off_t)sqe->off);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- READ_MULTISHOT: read into a provided buffer, re-armed by the
     * multishot loop.  Always buffer-select: each event consumes one buffer
     * from the group named by sqe->buf_group and reports it via F_BUFFER. --- */
    case IORING_OP_READ_MULTISHOT: {
        bool fixed;
        struct file *fp = resolve_fd(ctx, sqe->fd, sqe->flags, fixed);
        if (!fp) { res = -EBADF; break; }

        uint16_t bgid = sqe->buf_group;
        provided_buf pb{};
        bool found = false;
        WITH_LOCK(ctx->mtx) {
            found = io_uring_pick_buffer(ctx, bgid, &pb);
        }
        if (!found) { if (!fixed) fdrop(fp); res = -ENOBUFS; break; }

        struct iovec iov;
        iov.iov_base = pb.addr;
        iov.iov_len  = pb.len;
        size_t done = 0;
        int rc = sys_read(fp, &iov, 1, sqe->off, &done);
        if (!fixed) fdrop(fp);
        if (rc != 0) {
            res = -rc;
        } else {
            res = (int32_t)done;
            *out_cqe_flags = IORING_CQE_F_BUFFER | ((uint32_t)pb.bid << 16);
        }
        break;
    }

    /* --- FUTEX_WAIT: block until *uaddr != val or woken.
     * ABI: uaddr=sqe->addr, val=sqe->addr2, mask=sqe->addr3 (unused: OSv
     * futex has no bitset-any-value path here), futex flags in sqe->fd. --- */
    case IORING_OP_FUTEX_WAIT: {
        int *uaddr = reinterpret_cast<int *>(sqe->addr);
        int  val   = (int)sqe->addr2;
        int r = futex(uaddr, FUTEX_WAIT, val, nullptr, nullptr, 0);
        res = (r < 0) ? -errno : 0;
        break;
    }

    /* --- FUTEX_WAKE: wake up to `val` waiters on *uaddr. --- */
    case IORING_OP_FUTEX_WAKE: {
        int *uaddr = reinterpret_cast<int *>(sqe->addr);
        int  nwake = (int)sqe->addr2;
        int r = futex(uaddr, FUTEX_WAKE, nwake, nullptr, nullptr, 0);
        res = (r < 0) ? -errno : r;
        break;
    }

    /* --- MSG_RING: post a message to another io_uring's completion queue.
     * ABI: sqe->addr = command (IORING_MSG_DATA / IORING_MSG_SEND_FD),
     * sqe->fd = target ring fd, sqe->len = data (posted as target CQE res),
     * sqe->off = target CQE user_data.  Only IORING_MSG_DATA is supported;
     * SEND_FD has no OSv fixed-file-install-into-another-ring path. --- */
    case IORING_OP_MSG_RING: {
        if (sqe->addr != IORING_MSG_DATA) { res = -EINVAL; break; }
        struct file *tfp = nullptr;
        if (fget(sqe->fd, &tfp) != 0) { res = -EBADF; break; }
        auto *tctx = io_uring_ctx_from_file(tfp);
        if (!tctx) { fdrop(tfp); res = -EBADF; break; }
        WITH_LOCK(tctx->mtx) {
            io_uring_post_cqe_locked(tctx, sqe->off, (int32_t)sqe->len, 0);
            tctx->wait_cq.wake_all(tctx->mtx);
        }
        if (tctx->eventfd_fd >= 0) {
            uint64_t v = 1;
            (void)::write(tctx->eventfd_fd, &v, sizeof(v));
        }
        fdrop(tfp);
        res = 0;
        break;
    }

    /* --- SEND_ZC / SENDMSG_ZC: OSv has no zero-copy net path, so this is a
     * synchronous copying ::send / ::sendmsg.  To stay ABI-faithful we emit
     * the two-CQE sequence a real zero-copy send produces: a data CQE (bytes
     * sent, F_MORE) posted here, then the terminal notification CQE (res 0,
     * F_NOTIF) posted by io_uring_complete_op via *out_cqe_flags.  Because the
     * send already completed synchronously, the buffer is free immediately. --- */
    case IORING_OP_SEND_ZC: {
        void *buf = reinterpret_cast<void *>(sqe->addr);
        ssize_t r = ::send(sqe->fd, buf, sqe->len, (int)sqe->msg_flags);
        if (r < 0) { res = -(int)errno; break; }
        io_uring_post_multishot_cqe(ctx, sqe->user_data, (int32_t)r, 0);
        res = 0;
        *out_cqe_flags = IORING_CQE_F_NOTIF;
        break;
    }
    case IORING_OP_SENDMSG_ZC: {
        struct msghdr *msg = reinterpret_cast<struct msghdr *>(sqe->addr);
        ssize_t r = ::sendmsg(sqe->fd, msg, (int)sqe->msg_flags);
        if (r < 0) { res = -(int)errno; break; }
        io_uring_post_multishot_cqe(ctx, sqe->user_data, (int32_t)r, 0);
        res = 0;
        *out_cqe_flags = IORING_CQE_F_NOTIF;
        break;
    }

    /* --- READV_FIXED / WRITEV_FIXED: vectored r/w whose iovecs point into a
     * single registered buffer.  ABI: sqe->addr = iovec array, sqe->len =
     * iovcnt, sqe->buf_index = registered buffer.  Each iovec is bounds-checked
     * against the registered region before the syscall. --- */
    case IORING_OP_READV_FIXED:
    case IORING_OP_WRITEV_FIXED: {
        bool fixed;
        struct file *fp = resolve_fd(ctx, sqe->fd, sqe->flags, fixed);
        if (!fp) { res = -EBADF; break; }
        unsigned idx = sqe->buf_index;
        if (idx >= ctx->nr_registered_buffers) {
            if (!fixed) fdrop(fp);
            res = -EFAULT;
            break;
        }
        uintptr_t base = reinterpret_cast<uintptr_t>(ctx->registered_buffers[idx]);
        size_t    blen = ctx->registered_buffer_lens[idx];
        auto *iovp = reinterpret_cast<struct iovec *>(sqe->addr);
        size_t iovcnt = sqe->len;
        bool ok = true;
        for (size_t i = 0; i < iovcnt; i++) {
            uintptr_t p = reinterpret_cast<uintptr_t>(iovp[i].iov_base);
            size_t    l = iovp[i].iov_len;
            if (p < base || l > blen || p + l > base + blen) { ok = false; break; }
        }
        if (!ok) { if (!fixed) fdrop(fp); res = -EFAULT; break; }
        size_t done = 0;
        int rc = (sqe->opcode == IORING_OP_READV_FIXED)
                     ? sys_read(fp, iovp, iovcnt, sqe->off, &done)
                     : sys_write(fp, iovp, iovcnt, sqe->off, &done);
        if (!fixed) fdrop(fp);
        res = rc ? -rc : (int32_t)done;
        break;
    }

    /* --- FIXED_FD_INSTALL: turn a registered file slot into a real fd.
     * ABI: sqe->fd = registered slot index. --- */
    case IORING_OP_FIXED_FD_INSTALL: {
        unsigned slot = (unsigned)sqe->fd;
        if (slot >= ctx->nr_registered_files ||
            !ctx->registered_files[slot]) { res = -EBADF; break; }
        struct file *rf = ctx->registered_files[slot];
        fhold(rf);
        int newfd = -1;
        int rc = fdalloc(rf, &newfd);
        if (rc != 0) { fdrop(rf); res = -rc; break; }
        res = newfd;
        break;
    }

    /* --- EPOLL_WAIT: blocking wait on an epoll fd.
     * ABI: sqe->fd = epfd, sqe->addr = epoll_event array, sqe->len = maxevents.
     * Blocks indefinitely (the io-wq worker thread absorbs the block). --- */
    case IORING_OP_EPOLL_WAIT: {
        auto *evs = reinterpret_cast<struct epoll_event *>(sqe->addr);
        int r = ::epoll_wait(sqe->fd, evs, (int)sqe->len, -1);
        res = (r < 0) ? -errno : r;
        break;
    }

    /* --- Opcodes with no faithful OSv implementation: honest -ENOSYS rather
     * than a lying success.  WAITID (no waitid syscall), FUTEX_WAITV (OSv
     * futex has no vectored wait), RECV_ZC (no zero-copy rx), URING_CMD /
     * URING_CMD128 (no NVMe passthrough / SQE128 geometry yet). --- */
    case IORING_OP_WAITID:
    case IORING_OP_FUTEX_WAITV:
    case IORING_OP_RECV_ZC:
    case IORING_OP_URING_CMD:
    case IORING_OP_URING_CMD128:
        res = -ENOSYS;
        break;

    /* --- NOP128: SQE128 NOP.  SQE128 ring geometry is not implemented
     * (deferred to A3-4), so reject rather than silently treat as NOP. --- */
    case IORING_OP_NOP128:
        res = -EINVAL;
        break;

    /* --- xattr family: OSv has no extended-attribute syscalls --- */
    case IORING_OP_SETXATTR:
    case IORING_OP_FSETXATTR:
    case IORING_OP_GETXATTR:
    case IORING_OP_FGETXATTR:
        res = -ENOSYS;
        break;

    /* --- PIPE: OSv has no pipes --- */
    case IORING_OP_PIPE:
        res = -ENOSYS;
        break;

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
 * LINK_TIMEOUT (opcode 15) appears as the element immediately following the
 * op it guards in a linked chain: [linked_op, LINK_TIMEOUT].  We implement a
 * REAL race: the linked op runs (blocking) on this worker thread while a
 * helper thread waits until the timeout deadline.  Whichever finishes first
 * wins, decided exactly once under ctx->mtx:
 *   - op completes first  -> op posts its real result; LINK_TIMEOUT -> -ECANCELED
 *   - timer expires first -> helper fires the op's cancel token (interrupting
 *                            the blocked worker via A2-8); op -> -ECANCELED,
 *                            LINK_TIMEOUT -> -ETIME (or 0 if ETIME_SUCCESS).
 * IORING_TIMEOUT_ABS and the clock-selection flags are honored via
 * io_uring_deadline().  Note: an op blocked in a NON-interruptible wait (e.g.
 * disk I/O) cannot be broken mid-flight; if the timer wins in that case the
 * op still runs to completion but its CQE is reported as -ECANCELED, matching
 * the "timer fired first" outcome.
 *
 * A LINK_TIMEOUT that reaches the top-of-loop branch below (rather than being
 * consumed inline) is one whose guarded op was short-circuited (cancelled or
 * chain-failure propagated); Linux completes it with -ECANCELED.
 * ---------------------------------------------------------------------- */

/* Shared race state between a linked op's worker and its timeout helper. */
struct link_timeout_state {
    mutex   m;          /* guards op_done + the condvar */
    condvar cv;
    bool    op_done   = false;  /* set by worker when the op returns (m held) */
    bool    decided   = false;  /* winner chosen (ctx->mtx held) */
    bool    timer_won = false;  /* the winner (ctx->mtx held) */
};

/*
 * Is this SQE a multishot request?  Multishot re-arms and posts one CQE
 * (with IORING_CQE_F_MORE) per event until it errors or is cancelled.  The
 * flag lives in a different field per opcode:
 *   POLL_ADD -> sqe->len       (IORING_POLL_ADD_MULTI)
 *   ACCEPT   -> sqe->ioprio    (IORING_ACCEPT_MULTISHOT)
 *   RECV     -> sqe->ioprio    (IORING_RECV_MULTISHOT)
 *   READ_MULTISHOT -> opcode is itself multishot (opcode 49)
 */
static bool io_uring_is_multishot(const struct io_uring_sqe *sqe)
{
    switch (sqe->opcode) {
    case IORING_OP_POLL_ADD:
        return (sqe->len & IORING_POLL_ADD_MULTI) != 0;
    case IORING_OP_ACCEPT:
        return (sqe->ioprio & IORING_ACCEPT_MULTISHOT) != 0;
    case IORING_OP_RECV:
        return (sqe->ioprio & IORING_RECV_MULTISHOT) != 0;
    case IORING_OP_READ_MULTISHOT:
        return true;
    default:
        return false;
    }
}

/*
 * Run a multishot op to completion.  Repeatedly executes the op and posts an
 * intermediate CQE (F_MORE set) per event, re-arming until the op errors, is
 * cancelled, or the ring shuts down -- then posts a terminal CQE without
 * F_MORE.  Runs on the calling io-wq worker; blocking waits between events are
 * interruptible by a cancel (same worker-publish mechanism as single ops).
 *
 * ctx->mtx must NOT be held on entry.  The op has already been published as
 * cancellable and this worker set as its cancel.worker by the caller.
 */
static void exec_multishot(struct io_uring_ctx *ctx, io_uring_work &w)
{
    /* Retire the op: clear worker, unregister, post terminal (non-F_MORE) CQE. */
    auto retire = [&](int32_t res, uint32_t cqe_flags) {
        WITH_LOCK(ctx->mtx) {
            w.cancel.worker.store(nullptr, std::memory_order_release);
            auto range = ctx->cancellable.equal_range(w.sqe.user_data);
            for (auto it = range.first; it != range.second; ++it) {
                if (it->second == &w.cancel) { ctx->cancellable.erase(it); break; }
            }
        }
        io_uring_complete_op(ctx, w.sqe.user_data, res, cqe_flags);
    };

    for (;;) {
        if (w.cancel.cancelled.load(std::memory_order_relaxed) ||
            sched::thread::current()->interrupted() || ctx->shutdown) {
            retire(-ECANCELED, 0);
            return;
        }

        uint32_t cqe_flags = 0;
        int32_t  res = exec_single_sqe(ctx, &w.sqe, &cqe_flags);

        bool cancelled = w.cancel.cancelled.load(std::memory_order_relaxed);
        if (cancelled && (res == -EINTR || res == -ERESTART))
            res = -ECANCELED;

        /*
         * An error (including cancellation) terminates the multishot with a
         * final CQE that has no F_MORE.  A successful event posts an
         * intermediate CQE with F_MORE and re-arms.  RECV/ACCEPT/READ block
         * until a genuinely new event on the next iteration.
         */
        if (res < 0 || cancelled) {
            retire(res, cqe_flags);
            return;
        }

        io_uring_post_multishot_cqe(ctx, w.sqe.user_data, res, cqe_flags);

        /*
         * POLL_ADD multishot: OSv's ::poll is level-triggered and offers no
         * edge subscription, so a re-arm would immediately re-fire the same
         * readiness and spin.  Post one intermediate CQE then a terminal CQE
         * (res 0, no F_MORE) so the app re-arms explicitly -- honest
         * degradation.  RECV/ACCEPT/READ loop because they block per event.
         */
        if (w.sqe.opcode == IORING_OP_POLL_ADD) {
            retire(0, 0);
            return;
        }
    }
}

static void exec_sqe_chain(struct io_uring_ctx *ctx,
                            std::vector<io_uring_work> chain)
{
    bool chain_failed = false;

    /* Remove a work item from the cancellable map (ctx->mtx must be held). */
    auto unregister = [&](io_uring_work &w) {
        auto range = ctx->cancellable.equal_range(w.sqe.user_data);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == &w.cancel) {
                ctx->cancellable.erase(it);
                break;
            }
        }
    };

    for (size_t i = 0; i < chain.size(); i++) {
        io_uring_work &w = chain[i];

        /* An orphaned/short-circuited LINK_TIMEOUT: its guarded op did not run
         * (it was cancelled or chain-failure propagated).  Post -ECANCELED. */
        if (w.sqe.opcode == IORING_OP_LINK_TIMEOUT) {
            WITH_LOCK(ctx->mtx) { unregister(w); }
            io_uring_complete_op(ctx, w.sqe.user_data, -ECANCELED, 0);
            continue;
        }

        /* Check for async-cancel of this work item (before it starts) */
        if (w.cancel.cancelled.load(std::memory_order_relaxed)) {
            WITH_LOCK(ctx->mtx) { unregister(w); }
            io_uring_complete_op(ctx, w.sqe.user_data, -ECANCELED, 0);
            chain_failed = true;
            continue;
        }

        /* Propagate chain failure to non-hardlinked SQEs */
        if (chain_failed && !(w.sqe.flags & IOSQE_IO_HARDLINK)) {
            WITH_LOCK(ctx->mtx) { unregister(w); }
            io_uring_complete_op(ctx, w.sqe.user_data, -ECANCELED, 0);
            continue;
        }

        /* Does a LINK_TIMEOUT guard this op?  (It is the next chain element.) */
        bool has_lt = (i + 1 < chain.size()) &&
                      (chain[i + 1].sqe.opcode == IORING_OP_LINK_TIMEOUT);

        /*
         * Publish this thread as the op's worker so an in-flight cancel can
         * interrupt a blocking wait.  Re-check the cancelled flag under the
         * lock to close the race with a cancel that arrives right here, and
         * clear any stale interrupt before we begin.
         */
        bool pre_cancelled = false;
        WITH_LOCK(ctx->mtx) {
            if (w.cancel.cancelled.load(std::memory_order_relaxed)) {
                pre_cancelled = true;
                unregister(w);
            } else {
                sched::thread::current()->interrupted(false);
                w.cancel.worker.store(sched::thread::current(),
                                      std::memory_order_release);
            }
        }
        if (pre_cancelled) {
            io_uring_complete_op(ctx, w.sqe.user_data, -ECANCELED, 0);
            chain_failed = true;
            continue;
        }

        /*
         * Multishot ops re-arm and post one F_MORE CQE per event until they
         * error or are cancelled.  Linux never links a multishot op, so it is
         * handled standalone here: exec_multishot() runs the re-arm loop and
         * performs its own retire (clears worker, unregisters, posts terminal
         * CQE).  No LINK_TIMEOUT can guard it.
         */
        if (io_uring_is_multishot(&w.sqe)) {
            exec_multishot(ctx, w);
            continue;
        }

        /*
         * Arm the racing timeout helper, if a LINK_TIMEOUT guards this op.
         * The helper waits on st.cv until the op signals done or the deadline
         * passes; on deadline it wins the race (under ctx->mtx) and fires the
         * op's cancel token to break an interruptible blocking wait.
         */
        link_timeout_state st;
        sched::thread *lt_helper = nullptr;
        if (has_lt) {
            struct io_uring_sqe *lt_sqe = &chain[i + 1].sqe;
            auto *ts = reinterpret_cast<const struct __kernel_timespec *>(
                           lt_sqe->addr);
            auto deadline = io_uring_deadline(ts, lt_sqe->timeout_flags);
            cancel_token *tok = &w.cancel;
            lt_helper = sched::thread::make([ctx, &st, deadline, tok]() {
                bool timed_out = false;
                WITH_LOCK(st.m) {
                    while (!st.op_done) {
                        if (st.cv.wait(&st.m, deadline) == ETIMEDOUT) {
                            timed_out = !st.op_done;
                            break;
                        }
                    }
                }
                if (timed_out) {
                    WITH_LOCK(ctx->mtx) {
                        if (!st.decided) {
                            st.decided   = true;
                            st.timer_won = true;
                        }
                        if (st.timer_won)
                            cancel_token_fire(tok);
                    }
                }
            });
            lt_helper->start();
        }

        uint32_t cqe_flags = 0;
        int32_t  res       = exec_single_sqe(ctx, &w.sqe, &cqe_flags);

        /* Signal the helper (if any) that the op has completed. */
        if (has_lt) {
            WITH_LOCK(st.m) {
                st.op_done = true;
                st.cv.wake_all();
            }
        }

        /*
         * Retire the op: clear the worker pointer and unregister from the
         * cancellable map under the lock.  After this a late cancel only sets
         * the (now-ignored) flag and cannot interrupt an unrelated op.  If a
         * LINK_TIMEOUT is racing, decide the winner here (op side) too.
         */
        bool was_cancelled;
        WITH_LOCK(ctx->mtx) {
            w.cancel.worker.store(nullptr, std::memory_order_release);
            was_cancelled = w.cancel.cancelled.load(std::memory_order_relaxed);
            if (has_lt && !st.decided) {
                st.decided   = true;
                st.timer_won = false;
            }
            unregister(w);
        }

        if (has_lt) {
            lt_helper->join();
            delete lt_helper;
        }

        /*
         * Determine the op's CQE result.  If the timer won, the op is
         * cancelled regardless of what exec returned.  Otherwise, if the op
         * was cancelled while in flight and bailed with an interruption error,
         * normalize to -ECANCELED.
         */
        if (has_lt && st.timer_won)
            res = -ECANCELED;
        else if (was_cancelled && (res == -EINTR || res == -ERESTART))
            res = -ECANCELED;

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

        /*
         * Consume the guarding LINK_TIMEOUT inline: post its CQE reflecting the
         * race outcome and advance past it so the top-of-loop branch does not
         * also complete it.
         */
        if (has_lt) {
            io_uring_work &lt = chain[i + 1];
            WITH_LOCK(ctx->mtx) { unregister(lt); }
            int32_t lt_res;
            if (st.timer_won)
                lt_res = (lt.sqe.timeout_flags & IORING_TIMEOUT_ETIME_SUCCESS)
                             ? 0 : -ETIME;
            else
                lt_res = -ECANCELED;
            io_uring_complete_op(ctx, lt.sqe.user_data, lt_res, 0);
            i++;   /* skip the consumed LINK_TIMEOUT */
        }
    }
}

/* -------------------------------------------------------------------------
 * io-wq worker pool.
 *
 * A chain is classified as bounded (disk/file, WQ_BOUNDED, index 0) or
 * unbounded (net/poll/timeout, WQ_UNBOUNDED, index 1) by the opcode of its
 * first non-LINK_TIMEOUT entry.  Unbounded ops may block for an unbounded
 * time (recv/accept/poll/timeout), so they run in a separate worker pool to
 * keep prompt disk I/O from being starved of workers.
 *
 * Workers are persistent: each blocks on wq_wait[cls] for work, executes one
 * chain via exec_sqe_chain(), then loops.  A new worker is created lazily on
 * enqueue only when no worker is idle and the per-class cap wq_max_workers[cls]
 * has not been reached.  This bounds thread creation to the cap for the ring's
 * whole lifetime instead of one thread per submission.
 * ---------------------------------------------------------------------- */

static int io_uring_work_class(uint8_t opcode)
{
    switch (opcode) {
    case IORING_OP_POLL_ADD:
    case IORING_OP_POLL_REMOVE:
    case IORING_OP_SYNC_FILE_RANGE:  /* fsync can block on the backing dev */
    case IORING_OP_SENDMSG:
    case IORING_OP_RECVMSG:
    case IORING_OP_TIMEOUT:
    case IORING_OP_TIMEOUT_REMOVE:
    case IORING_OP_ACCEPT:
    case IORING_OP_ASYNC_CANCEL:
    case IORING_OP_LINK_TIMEOUT:
    case IORING_OP_CONNECT:
    case IORING_OP_SEND:
    case IORING_OP_RECV:
    case IORING_OP_EPOLL_CTL:
    case IORING_OP_SHUTDOWN:
    case IORING_OP_SOCKET:
    case IORING_OP_BIND:
    case IORING_OP_LISTEN:
    case IORING_OP_SEND_ZC:      /* copying ::send, may block on socket buffer */
    case IORING_OP_SENDMSG_ZC:
    case IORING_OP_EPOLL_WAIT:   /* blocks until an epoll event is ready */
    case IORING_OP_FUTEX_WAIT:   /* blocks until woken */
        return 1;   /* WQ_UNBOUNDED */
    default:
        return 0;   /* WQ_BOUNDED */
    }
}

static uint8_t chain_lead_opcode(const std::vector<io_uring_work> &chain)
{
    for (const auto &w : chain)
        if (w.sqe.opcode != IORING_OP_LINK_TIMEOUT)
            return w.sqe.opcode;
    return chain.empty() ? (uint8_t)IORING_OP_NOP : chain.front().sqe.opcode;
}

/* Persistent worker loop for pool class `cls`.  ctx->mtx must NOT be held. */
static void io_uring_wq_worker(struct io_uring_ctx *ctx, int cls)
{
    WITH_LOCK(ctx->mtx) {
        for (;;) {
            while (ctx->wq_queue[cls].empty() && !ctx->shutdown) {
                ctx->wq_idle[cls]++;
                ctx->wq_wait[cls].wait(ctx->mtx);
                ctx->wq_idle[cls]--;
            }
            if (ctx->shutdown && ctx->wq_queue[cls].empty())
                return;

            auto cp = std::move(ctx->wq_queue[cls].front());
            ctx->wq_queue[cls].pop_front();

            DROP_LOCK(ctx->mtx) {
                exec_sqe_chain(ctx, std::move(*cp));
            }
        }
    }
}

/*
 * Hand a chain to the worker pool.  ctx->mtx must be held.  The chain has
 * already been registered in ctx->cancellable and counted in pending_ops by
 * the caller.  Spawns a new worker only when none is idle and the class cap
 * is not yet reached.
 */
static void io_uring_wq_enqueue(struct io_uring_ctx *ctx,
                                std::shared_ptr<std::vector<io_uring_work>> cp)
{
    int cls = io_uring_work_class(chain_lead_opcode(*cp));
    ctx->wq_queue[cls].push_back(std::move(cp));

    if (ctx->wq_idle[cls] == 0 &&
        ctx->wq_nr_workers[cls] < ctx->wq_max_workers[cls]) {
        ctx->wq_nr_workers[cls]++;
        auto *t = sched::thread::make(
            [ctx, cls]() { io_uring_wq_worker(ctx, cls); });
        ctx->wq_threads.push_back(t);
        t->start();
    } else {
        ctx->wq_wait[cls].wake_one(ctx->mtx);
    }
}

/* -------------------------------------------------------------------------
 * Read SQEs from the ring and dispatch chains to the io-wq worker pool.
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
        /*
         * IORING_SETUP_R_DISABLED: reject submissions until the app issues
         * IORING_REGISTER_ENABLE_RINGS.  Matches the kernel gating semantics.
         */
        if (ctx->disabled)
            return -EBADFD;

        uint32_t head, tail;
        if (ctx->sq_ring) {
            auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
            head = sq->head.load(std::memory_order_acquire);
            tail = sq->tail.load(std::memory_order_acquire);
        } else {
            head = ctx->sq.head;
            tail = ctx->sq.tail;
        }

        auto dispatch_chain = [&](std::vector<io_uring_work> chain) {
            ctx->pending_ops += (uint32_t)chain.size();

            /* Register all items in the chain as cancellable */
            for (auto &w : chain) {
                ctx->cancellable.emplace(w.sqe.user_data, &w.cancel);
            }

            /* shared_ptr makes the work item copyable into the queue */
            auto cp = std::make_shared<std::vector<io_uring_work>>(
                std::move(chain));
            io_uring_wq_enqueue(ctx, std::move(cp));
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
            sq->head.store(head, std::memory_order_release);
        }
        ctx->sq.head = head;
    }

    return (int)submitted;
}

/* -------------------------------------------------------------------------
 * Wait for at least min_complete CQEs to be available.
 *
 * If deadline_valid is true, the wait is bounded by deadline (an absolute
 * osv::clock::uptime point); on expiry we return -ETIME if fewer than
 * min_complete CQEs are ready.  This backs the io_uring_enter() EXT_ARG
 * timeout (A2-12).  A min_wait floor is applied via min_deadline: the wait is
 * not cut short before that point even when the main deadline is sooner.
 * ---------------------------------------------------------------------- */

static int io_uring_wait_cqe(struct io_uring_ctx *ctx, unsigned min_complete,
                             bool deadline_valid,
                             osv::clock::uptime::time_point deadline)
{
    sched::timer tmr(*sched::thread::current());
    if (deadline_valid)
        tmr.set(deadline);

    WITH_LOCK(ctx->mtx) {
        while (true) {
            if (ctx->shutdown)
                return -EINVAL;

            /* Pull any backlogged CQEs into slots the app just freed. */
            flush_cq_overflow_locked(ctx);

            uint32_t head, tail;
            if (ctx->cq_ring) {
                auto *cq = static_cast<struct io_uring_cq_ring *>(ctx->cq_ring);
                head = cq->head.load(std::memory_order_acquire);
                tail = cq->tail.load(std::memory_order_acquire);
            } else {
                head = ctx->cq.head;
                tail = ctx->cq.tail;
            }

            if ((tail - head) >= min_complete)
                return 0;

            if (ctx->pending_ops == 0 && (tail - head) > 0)
                return 0;

            if (deadline_valid && tmr.expired())
                return (tail - head) >= min_complete ? 0 : -ETIME;

            if (deadline_valid)
                sched::thread::wait_for(ctx->mtx, ctx->wait_cq, tmr);
            else
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
            head = sq->head.load(std::memory_order_acquire);
            tail = sq->tail.load(std::memory_order_acquire);
        } else {
            WITH_LOCK(ctx->mtx) { head = ctx->sq.head; tail = ctx->sq.tail; }
        }

        if (head != tail) {
            if (ctx->sq_ring) {
                auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
                sq->flags.fetch_and(~IORING_SQ_NEED_WAKEUP, std::memory_order_release);
            }
            io_uring_submit_sqes(ctx, ctx->sq.entries);
            last_active = clock::now();
        } else {
            if (clock::now() - last_active >= idle_limit) {
                if (ctx->sq_ring) {
                    auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
                    sq->flags.fetch_or(IORING_SQ_NEED_WAKEUP, std::memory_order_release);
                }
                WITH_LOCK(ctx->mtx) {
                    while (!ctx->shutdown) {
                        if (ctx->sq_ring) {
                            auto *sq = static_cast<struct io_uring_sq_ring *>(ctx->sq_ring);
                            if (sq->tail.load(std::memory_order_acquire) != head)
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

/* See forward declaration near the top of the file. */
static struct io_uring_ctx *io_uring_ctx_from_file(struct file *fp)
{
    if (!dynamic_cast<io_uring_file *>(fp))
        return nullptr;
    return static_cast<struct io_uring_ctx *>(fp->f_data);
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
        /* Reject a mapping that is smaller than the ring or larger than the
         * page-aligned allocation; an oversized mapping would let map_page()
         * map pages past the ring buffer and expose unrelated memory. */
        if (size < need || size > align_up(need, mmu::page_size))
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
        if (size < need || size > align_up(need, mmu::page_size))
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
        if (size < need || size > align_up(need, mmu::page_size))
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
        _ctx->wq_wait[0].wake_all(_ctx->mtx);
        _ctx->wq_wait[1].wake_all(_ctx->mtx);
    }

    if (_ctx->sq_poll_thread) {
        _ctx->sq_poll_thread->join();
        delete _ctx->sq_poll_thread;
        _ctx->sq_poll_thread = nullptr;
    }

    /*
     * Join every io-wq worker.  A worker either exits promptly (idle, woken by
     * the wake_all above) or after finishing its current chain: exec_sqe_chain
     * observes ctx->shutdown (POLL_ADD bails, in-flight interruptible ops keep
     * running to completion) and returns, then the worker sees an empty queue
     * with shutdown set and exits.  Joining here guarantees no worker touches
     * the ctx after we start freeing it below.
     */
    for (auto *t : _ctx->wq_threads) {
        t->join();
        delete t;
    }
    _ctx->wq_threads.clear();

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
    if (_ctx->registered_buffer_lens) {
        delete[] _ctx->registered_buffer_lens;
        _ctx->registered_buffer_lens = nullptr;
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

/* The io_uring_*_impl() functions implement the syscalls and return a negative
 * errno directly on failure (Linux kernel style).  The sys_io_uring_*()
 * wrappers below additionally set errno so that syscall_wrapper() in linux.cc
 * reconstructs the Linux syscall ABI return correctly -- see the sys_ wrapper
 * comment below for why this matters to liburing. */
static long io_uring_setup_impl(unsigned entries, struct io_uring_params *params);
static int  io_uring_enter_impl(int fd, unsigned to_submit, unsigned min_complete,
                                unsigned flags, const void *sig, size_t sigsz);
static int  io_uring_register_impl(int fd, unsigned opcode, void *arg,
                                   unsigned nr_args);

/* -------------------------------------------------------------------------
 * sys_io_uring_setup
 *
 * io_uring_setup_impl() returns a negative errno directly on failure (Linux
 * kernel style), which the in-tree self-tests assert on by calling it through
 * the sys_ wrapper below.  The sys_ wrapper additionally sets errno, because
 * syscall_wrapper() in linux.cc reconstructs the Linux syscall ABI return as
 * -errno for any negative result -- without errno set it would translate a
 * -EINVAL into 0 (a false success), which makes liburing's
 * io_uring_queue_init_mem() probe believe an unsupported ring layout works.
 * ---------------------------------------------------------------------- */

static long io_uring_setup_impl(unsigned entries, struct io_uring_params *params)
{
    if (entries < MIN_ENTRIES || entries > MAX_ENTRIES)
        return -EINVAL;
    if (!params)
        return -EFAULT;

    /*
     * Accepted setup flags.  A flag is accepted only if we honor its contract
     * (honesty rule: a lie is worse than -EINVAL):
     *   CQSIZE/CLAMP        - ring sizing, honored below.
     *   SQPOLL/SQ_AFF       - kernel-side submit thread, pinned per sq_thread_cpu.
     *   IOPOLL              - app reaps completions via enter(GETEVENTS); workers
     *                         still post CQEs, so completions arrive and are
     *                         reaped.  No busy-poll needed for correctness.
     *   ATTACH_WQ           - each ring keeps its own io-wq pool; sharing is a
     *                         resource optimization, not an observable contract.
     *   SUBMIT_ALL          - our submit loop already never aborts a batch on a
     *                         failing SQE (bad ops fail at exec and post their own
     *                         CQE), so the contract already holds.
     *   COOP_TASKRUN/TASKRUN_FLAG/DEFER_TASKRUN
     *                       - completions are posted directly by workers + eventfd;
     *                         there is no deferred task-work queue to coordinate,
     *                         so "completions become visible" already holds.
     *   SINGLE_ISSUER       - a promise from the app; we impose no extra locking.
     *   R_DISABLED          - honored: ring starts disabled, ENABLE_RINGS clears it.
     *
     * Rejected (would require SQE/CQE geometry or mmap-ownership changes that
     * pervade indexing and the setup mmap contract; faking them corrupts memory):
     *   SQE128, CQE32, SQE_MIXED, CQE_MIXED, HYBRID_IOPOLL, SQ_REWIND,
     *   NO_MMAP, REGISTERED_FD_ONLY, NO_SQARRAY.
     */
    const uint32_t supported_flags = IORING_SETUP_CQSIZE
                                   | IORING_SETUP_SQPOLL
                                   | IORING_SETUP_SQ_AFF
                                   | IORING_SETUP_CLAMP
                                   | IORING_SETUP_IOPOLL
                                   | IORING_SETUP_ATTACH_WQ
                                   | IORING_SETUP_SUBMIT_ALL
                                   | IORING_SETUP_COOP_TASKRUN
                                   | IORING_SETUP_TASKRUN_FLAG
                                   | IORING_SETUP_SINGLE_ISSUER
                                   | IORING_SETUP_DEFER_TASKRUN
                                   | IORING_SETUP_R_DISABLED;
    if (params->flags & ~supported_flags)
        return -EINVAL;

    /* Round up to next power of two (1 stays 1; clz-based round-up below
     * would otherwise turn 1 into 2 because it feeds 1 into __builtin_clz). */
    entries = round_up_pow2(entries);
    if (entries > MAX_ENTRIES && (params->flags & IORING_SETUP_CLAMP))
        entries = MAX_ENTRIES;

    auto *ctx = new io_uring_ctx;

    ctx->setup_flags   = params->flags;
    ctx->sq_thread_cpu = params->sq_thread_cpu;
    ctx->disabled      = (params->flags & IORING_SETUP_R_DISABLED) != 0;

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
        cq_entries = round_up_pow2(cq_entries);
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
        /*
         * Advertise only feature bits we actually honor (honesty rule):
         *   NODROP          - CQ overflow backlog, never silently dropped (A2-4).
         *   SUBMIT_STABLE   - SQEs are copied at submit; app may reuse them.
         *   RW_CUR_POS      - offset -1 uses the file's current position.
         *   FAST_POLL       - POLL_ADD-backed readiness for pollable ops (A2-3).
         *   SQPOLL_NONFIXED - SQPOLL works without pre-registered files.
         *   CQE_SKIP        - IOSQE_CQE_SKIP_SUCCESS is honored at completion.
         *   NATIVE_WORKERS  - real persistent io-wq worker pool (A2-1).
         * SINGLE_MMAP is deliberately NOT advertised: we allocate the SQ ring,
         * CQ ring, and SQE array as separate regions, so clients must perform
         * the three-mmap dance rather than one combined mapping.
         */
        params->features   = IORING_FEAT_NODROP
                           | IORING_FEAT_SUBMIT_STABLE
                           | IORING_FEAT_RW_CUR_POS
                           | IORING_FEAT_FAST_POLL
                           | IORING_FEAT_SQPOLL_NONFIXED
                           | IORING_FEAT_CQE_SKIP
                           | IORING_FEAT_NATIVE_WORKERS;

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
            /*
             * IORING_SETUP_SQ_AFF: pin the poll thread to sq_thread_cpu.
             * Ignore an out-of-range CPU rather than fail setup (the ring is
             * otherwise fully functional; the affinity hint is advisory).
             */
            if ((params->flags & IORING_SETUP_SQ_AFF) &&
                ctx->sq_thread_cpu < sched::cpus.size()) {
                sched::thread::pin(ctx->sq_poll_thread,
                                   sched::cpus[ctx->sq_thread_cpu]);
            }
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

extern "C"
OSV_LIBC_API
long sys_io_uring_setup(unsigned entries, struct io_uring_params *params)
{
    long r = io_uring_setup_impl(entries, params);
    if (r < 0)
        errno = -r;
    return r;
}

/* -------------------------------------------------------------------------
 * sys_io_uring_enter
 * ---------------------------------------------------------------------- */

extern "C"
OSV_LIBC_API
int sys_io_uring_enter(int fd, unsigned to_submit, unsigned min_complete,
                       unsigned flags, const void *sig, size_t sigsz)
{
    int r = io_uring_enter_impl(fd, to_submit, min_complete, flags, sig, sigsz);
    if (r < 0)
        errno = -r;
    return r;
}

static int io_uring_enter_impl(int fd, unsigned to_submit, unsigned min_complete,
                               unsigned flags, const void *sig, size_t sigsz)
{
    /*
     * IORING_ENTER_REGISTERED_RING: fd is an index into the registered-ring-fd
     * table.  We reject IORING_REGISTER_RING_FDS (A3-6), so no such table
     * exists and any index would be meaningless -- reject rather than
     * misinterpret a raw fd as an index.
     */
    if (flags & IORING_ENTER_REGISTERED_RING)
        return -EINVAL;

    struct file *fp;
    int error = fget(fd, &fp);
    if (error)
        return -error;

    auto *io_uring_fp = dynamic_cast<io_uring_file *>(fp);
    if (!io_uring_fp) {
        fdrop(fp);
        return -EINVAL;
    }

    auto *ctx = static_cast<struct io_uring_ctx *>(fp->f_data);

    /*
     * Resolve the getevents wait bound from sig/sigsz (A2-12).
     *
     *   IORING_ENTER_EXT_ARG: sig points to a struct io_uring_getevents_arg
     *   (sigsz must equal its size) carrying an optional relative timeout
     *   (ts, a __kernel_timespec) and a min_wait_usec floor.  Its sigmask is
     *   ignored: OSv delivers no POSIX signals to the blocked thread, so there
     *   is no mask to apply -- masking would be a no-op we would rather not
     *   pretend to honor.
     *
     *   Without EXT_ARG: sig is a raw sigset_t of size sigsz.  Linux requires
     *   sigsz == sizeof(sigset_t); we validate that (rejecting a malformed
     *   call) and then ignore the mask for the same reason.
     */
    bool deadline_valid = false;
    osv::clock::uptime::time_point deadline;

    if (flags & IORING_ENTER_EXT_ARG) {
        if (!sig || sigsz != sizeof(struct io_uring_getevents_arg)) {
            fdrop(fp);
            return -EINVAL;
        }
        auto *arg = static_cast<const struct io_uring_getevents_arg *>(sig);
        osv::clock::uptime::duration wait_dur(0);
        bool have_wait = false;
        if (arg->ts) {
            auto *ts = reinterpret_cast<const struct __kernel_timespec *>(arg->ts);
            wait_dur = std::chrono::seconds(ts->tv_sec)
                     + std::chrono::nanoseconds(ts->tv_nsec);
            have_wait = true;
        }
        if (arg->min_wait_usec) {
            auto floor = std::chrono::duration_cast<osv::clock::uptime::duration>(
                std::chrono::microseconds(arg->min_wait_usec));
            if (!have_wait || floor > wait_dur) {
                wait_dur = floor;
                have_wait = true;
            }
        }
        if (have_wait) {
            deadline = osv::clock::uptime::now() + wait_dur;
            deadline_valid = true;
        }
    } else if (sig) {
        if (sigsz != sizeof(sigset_t)) {
            fdrop(fp);
            return -EINVAL;
        }
    }

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
        int err = io_uring_wait_cqe(ctx, min_complete, deadline_valid, deadline);
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
    int r = io_uring_register_impl(fd, opcode, arg, nr_args);
    if (r < 0)
        errno = -r;
    return r;
}

static int io_uring_register_impl(int fd, unsigned opcode, void *arg, unsigned nr_args)
{
    struct file *fp;
    int error = fget(fd, &fp);
    if (error)
        return -error;

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
            if (!arg) { ret = -EFAULT; break; }

            auto *iovecs = static_cast<struct iovec *>(arg);
            ctx->registered_buffers = new void *[nr_args];
            ctx->registered_buffer_lens = new size_t[nr_args];
            for (unsigned i = 0; i < nr_args; i++) {
                ctx->registered_buffers[i] = iovecs[i].iov_base;
                ctx->registered_buffer_lens[i] = iovecs[i].iov_len;
            }
            ctx->nr_registered_buffers = nr_args;
            break;
        }

        case IORING_UNREGISTER_BUFFERS:
            if (!ctx->registered_buffers) { ret = -EINVAL; break; }
            delete[] ctx->registered_buffers;
            delete[] ctx->registered_buffer_lens;
            ctx->registered_buffers      = nullptr;
            ctx->registered_buffer_lens  = nullptr;
            ctx->nr_registered_buffers   = 0;
            break;

        case IORING_REGISTER_FILES: {
            if (ctx->registered_files) { ret = -EBUSY; break; }
            if (nr_args == 0 || nr_args > 1024) { ret = -EINVAL; break; }
            if (!arg) { ret = -EFAULT; break; }

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
             * Register a live buffer ring for a buffer group.  The ring is an
             * array of io_uring_buf entries maintained by the application; the
             * kernel side reads ring->tail fresh on each buffer pick and
             * advances its own head counter (see io_uring_pick_buffer).  We do
             * NOT copy entries -- the ring stays live so buffers the app adds
             * after registration are visible.
             */
            if (!arg) { ret = -EFAULT; break; }
            auto *reg = static_cast<struct io_uring_buf_reg *>(arg);
            uint32_t nent = reg->ring_entries;
            /*
             * The ring is indexed with a mask of (nent - 1), so nent must be
             * a nonzero power of two, and the ring address must be valid.
             */
            if (!reg->ring_addr || nent == 0 || (nent & (nent - 1)) != 0) {
                ret = -EINVAL;
                break;
            }
            auto *ring = reinterpret_cast<struct io_uring_buf_ring *>(
                             reg->ring_addr);
            uint16_t bgid = reg->bgid;

            if (!ring || nent == 0 || (nent & (nent - 1)) != 0) {
                ret = -EINVAL;
                break;
            }

            buf_ring br;
            br.ring = ring;
            br.mask = nent - 1;
            br.head = 0;
            ctx->buf_rings[bgid] = br;
            break;
        }

        case IORING_UNREGISTER_PBUF_RING: {
            if (!arg) { ret = -EFAULT; break; }
            auto *reg = static_cast<struct io_uring_buf_reg *>(arg);
            ctx->buf_rings.erase(reg->bgid);
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
                cancel_token_fire(it->second);
                found = true;
                break;
            }
            ret = found ? 0 : -ENOENT;
            break;
        }

        case IORING_REGISTER_ENABLE_RINGS:
            /*
             * Clears IORING_SETUP_R_DISABLED gating so submissions are accepted.
             * Idempotent: enabling an already-enabled ring is not an error.
             */
            ctx->disabled = false;
            break;

        case IORING_REGISTER_IOWQ_MAX_WORKERS: {
            /*
             * arg -> uint32_t[2] = { bounded_max, unbounded_max }.  A zero entry
             * means "leave unchanged" and the current value is returned in place
             * (Linux semantics).  Wires directly to the io-wq pool caps.
             */
            if (!arg || nr_args != 2) { ret = -EINVAL; break; }
            auto *vals = static_cast<uint32_t *>(arg);
            for (int c = 0; c < 2; c++) {
                uint32_t prev = ctx->wq_max_workers[c];
                if (vals[c] != 0)
                    ctx->wq_max_workers[c] = vals[c];
                vals[c] = prev;
            }
            break;
        }

        case IORING_REGISTER_IOWQ_AFF:
        case IORING_UNREGISTER_IOWQ_AFF:
            /*
             * io-wq CPU affinity mask.  OSv's scheduler load-balances worker
             * threads across CPUs and we expose no per-pool cpumask pinning, so
             * accept-and-ignore rather than lie about honoring a specific mask
             * or fail a ring that is otherwise fully functional.
             */
            break;

        case IORING_REGISTER_RING_FDS:
        case IORING_UNREGISTER_RING_FDS:
            /*
             * Registered-ring-fd table (liburing default since 5.18) is a
             * syscall-overhead optimization: enter() takes a small ring index
             * instead of a raw fd.  liburing's io_uring_register_ring_fd()
             * expects the kernel to write the assigned index back into
             * arg->offset and then passes that index with
             * IORING_ENTER_REGISTERED_RING.  OSv has no such per-thread index
             * table, so reporting success without one would hand liburing a
             * garbage index -- a lie worse than failure.  Reject so liburing
             * keeps INT_FLAG_REG_RING unset and continues using the raw fd.
             */
            ret = -EINVAL;
            break;

        /*
         * Honest rejections: declared in the ABI header but not implemented.
         * Advertising success here would corrupt state or silently drop the
         * caller's intent (personalities, restrictions, resource tagging,
         * NAPI, zero-copy rx, BPF, ring resize/clone) -- a lie is worse than
         * -EINVAL, so reject explicitly.
         */
        case IORING_REGISTER_PERSONALITY:
        case IORING_UNREGISTER_PERSONALITY:
        case IORING_REGISTER_RESTRICTIONS:
        case IORING_REGISTER_FILES2:
        case IORING_REGISTER_FILES_UPDATE2:
        case IORING_REGISTER_BUFFERS2:
        case IORING_REGISTER_BUFFERS_UPDATE:
        case IORING_REGISTER_FILE_ALLOC_RANGE:
        case IORING_REGISTER_PBUF_STATUS:
        case IORING_REGISTER_NAPI:
        case IORING_UNREGISTER_NAPI:
        case IORING_REGISTER_CLOCK:
        case IORING_REGISTER_CLONE_BUFFERS:
        case IORING_REGISTER_SEND_MSG_RING:
        case IORING_REGISTER_ZCRX_IFQ:
        case IORING_REGISTER_RESIZE_RINGS:
        case IORING_REGISTER_MEM_REGION:
        case IORING_REGISTER_QUERY:
        case IORING_REGISTER_ZCRX_CTRL:
        case IORING_REGISTER_BPF_FILTER:
            ret = -ENOSYS;
            break;

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
