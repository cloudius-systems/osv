/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

//Most of the code in this file is modeled after corresponding vnops
//implementation in ZFS, RoFS and RamFS filesystems but also loosely
//after the code in src/ext4.c from lwext4 library. The internal functions
//ext_internal_read() and ext_internal_write() on other hand are almost verbatim
//taken from ext4_read() and ext4_write() from the same file and slighly
//adjusted to C++.
//
//In effect, this vnops implementation bypasses the ext4.c layer of the lwext4
//library and interacts with lower-layer functions like ext4_block_*(), ext4_dir_*(),
//ext4_fs_*() and ext4_inode_*() in a similar way the original ext4.c does.
//
//The libext does not implement journal (we can integrate it later and make it optional)
//nor xattr which is not even supported by OSv VFS layer.

// The libext module is built with -fno-rtti, but <osv/pagecache.hh> pulls in
// <osv/mmu.hh> -> <osv/trace.hh> which uses typeid.  We only need a couple of
// symbols from the page cache to warm it in ext_map_cached_page(), so declare
// them minimally here instead of including the heavy headers.  The uio carries
// the pagecache hashkey opaquely, so we never need its layout.
#include <osv/pagealloc.hh>   // memory::alloc_page / free_page (light header)
namespace pagecache {
    struct hashkey;
    void map_read_cached_page(hashkey *key, void *page);
}
static const unsigned long EXT_PAGE_SIZE = 4096;

extern "C" {
#define USE_C_INTERFACE 1
#include <osv/device.h>
#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/mount.h>
#include <osv/debug.h>
#include <osv/file.h>
#include <osv/vnode_attr.h>
#include <osv/mutex.h>

void* alloc_contiguous_aligned(size_t size, size_t align);
void free_contiguous_aligned(void* p);
}

#include <ext4_errno.h>
#include <ext4_dir.h>
#include <ext4_inode.h>
#include <ext4_fs.h>
#include <ext4_blockdev.h>
#include <ext4_dir_idx.h>
#include <ext4_trans.h>

#include <cstdlib>
#include <time.h>
#include <cstddef>
#include <cassert>
#include <pthread.h>

#include <algorithm>
#include <set>

//#define CONF_debug_ext 1
#if CONF_debug_ext
#define ext_debug(format,...) kprintf("[ext4] " format, ##__VA_ARGS__)
#else
#define ext_debug(...)
#endif

// Mark an inode as deleted with a non-zero deletion time before it is freed, so
// Linux e2fsck does not flag "deleted inode has zero dtime".  lwext4's own
// delete path uses ext4_inode_set_del_time(inode, -1L), but that symbol is not
// exported from liblwext4.so, so set the field directly.  0xffffffff is
// byte-order invariant, so no to_le32() is needed.
static inline void ext_mark_inode_deleted(struct ext4_inode *inode)
{
    inode->deletion_time = 0xffffffff;
}

//Simple RAII struct to automate release of i-node reference
//when it goes out of scope.
struct auto_inode_ref {
    struct ext4_inode_ref _ref;
    int _r;

    auto_inode_ref(struct ext4_fs *fs, uint32_t inode_no) {
        _r = ext4_fs_get_inode_ref(fs, inode_no, &_ref);
    }
    ~auto_inode_ref() {
        if (_r == EOK) {
            ext4_fs_put_inode_ref(&_ref);
        }
    }
};

struct ext_vdata {
    int ref_count;
    bool delete_on_last_close;

    // Double-buffered ASYNC sequential read-ahead. lwext4's read path issues one
    // synchronous bio per read() with no prefetch, so many small sequential
    // reads leave the NVMe queue idle during the app's compute (the seq-read
    // gap vs Linux, which keeps the device busy via async prefetch).
    //
    // Design: two 1 MiB windows, "cur" (being served to the app) and "next"
    // (being prefetched). A dedicated worker thread fills "next" while the app
    // consumes "cur". When a read crosses out of "cur", we swap next->cur and
    // ask the worker to prefetch the following window. So by the time the app
    // needs the next bytes they are usually already in memory, and the device
    // stays busy.  All state is under ra_lock. Any write/truncate invalidates
    // both windows; ~ext_vdata joins and frees the worker.
    //
    // ponytail: one worker pthread per open vnode, not a shared pool. Correct
    // and keeps the device busy; a bounded pool is the upgrade if thousands of
    // files are read sequentially at once.
    mutex_t ra_lock;             // protects all fields below except the worker
                                 // handshake fields (those use ra_cvmtx)
    char *cur_buf;               // owned; window currently served to the app
    uint64_t cur_start;
    size_t cur_len;
    char *next_buf;              // owned; window being / to be prefetched
    uint64_t next_start;         // file offset the worker should prefetch
    size_t next_len;             // valid bytes in next_buf once ready
    bool next_ready;             // worker finished filling next_buf
    bool next_pending;           // worker asked to fill next_buf (in flight)
    uint64_t ra_next;            // expected offset of next sequential read
    uint32_t inode_no;           // for the worker to get its own inode_ref
    struct ext4_fs *fs;          // filesystem the worker reads from

    // Worker thread handshake (separate mutex/cond so the worker never blocks
    // on ra_lock while the read path holds it, and the read path never blocks
    // on the worker while holding ra_lock).
    pthread_t worker;
    bool worker_started;
    pthread_mutex_t ra_cvmtx;
    pthread_cond_t ra_req_cv;    // signalled to give the worker work
    pthread_cond_t ra_done_cv;   // signalled by the worker when done
    bool req_work;               // there is a prefetch request for the worker
    bool worker_busy;            // worker is currently reading
    bool shutdown;               // tell the worker to exit
    // Prefetch request the read path hands to the worker:
    char *req_buf;
    uint64_t req_off;
    size_t req_len;
    // Result the worker hands back:
    size_t req_got;
    int req_err;

    ext_vdata() {
        ref_count = 0;
        delete_on_last_close = false;
        mutex_init(&ra_lock);
        cur_buf = nullptr;
        cur_start = 0;
        cur_len = 0;
        next_buf = nullptr;
        next_start = 0;
        next_len = 0;
        next_ready = false;
        next_pending = false;
        ra_next = 0;
        inode_no = 0;
        fs = nullptr;
        worker_started = false;
        pthread_mutex_init(&ra_cvmtx, nullptr);
        pthread_cond_init(&ra_req_cv, nullptr);
        pthread_cond_init(&ra_done_cv, nullptr);
        req_work = false;
        worker_busy = false;
        shutdown = false;
        req_buf = nullptr;
        req_off = 0;
        req_len = 0;
        req_got = 0;
        req_err = 0;
    }

    ~ext_vdata() {
        // Tell the worker to exit and wait for it, so nothing touches the
        // buffers after we free them.
        if (worker_started) {
            pthread_mutex_lock(&ra_cvmtx);
            shutdown = true;
            pthread_cond_signal(&ra_req_cv);
            pthread_mutex_unlock(&ra_cvmtx);
            pthread_join(worker, nullptr);
        }
        pthread_cond_destroy(&ra_req_cv);
        pthread_cond_destroy(&ra_done_cv);
        pthread_mutex_destroy(&ra_cvmtx);
        if (cur_buf) {
            free_contiguous_aligned(cur_buf);
        }
        if (next_buf) {
            free_contiguous_aligned(next_buf);
        }
    }
};

typedef	struct vnode vnode_t;
typedef	struct file file_t;
typedef struct uio uio_t;
typedef	off_t offset_t;
typedef	struct vattr vattr_t;

#define EXT4_EPOCH_BITS 2
#define EXT4_EPOCH_MASK ((1 << EXT4_EPOCH_BITS) - 1)
#define EXT4_NSEC_MASK (~0UL << EXT4_EPOCH_BITS)

#define set_inode_time(inode, time, type, extra_avail) { \
    ext4_inode_set_ ## type ## _time(inode, (int32_t)time.tv_sec); \
    if (extra_avail) { \
        uint32_t extra_time = ((time.tv_sec - (int32_t)time.tv_sec) >> 32) & EXT4_EPOCH_MASK; \
        ext4_inode_set_extra_ ## type ## _time(inode, extra_time | (time.tv_nsec << EXT4_EPOCH_BITS)); \
    } \
}

#define get_inode_time(inode, time, type, extra_avail) { \
    time.tv_sec = ext4_inode_get_ ## type ## _time(inode); \
    if (extra_avail) { \
        uint32_t extra_time = ext4_inode_get_extra_ ## type ## _time(inode); \
        time.tv_sec += (uint64_t)(extra_time & EXT4_EPOCH_MASK) << 32; \
        time.tv_nsec = (extra_time & EXT4_NSEC_MASK) >> EXT4_EPOCH_BITS; \
    } \
}

//TODO:
//Ops:
// - ext_ioctl
//
// Later:
// - ext_fallocate - Linux specific

static int
ext_open(struct file *fp)
{
    struct vnode *vp = file_dentry(fp)->d_vnode;
    ext_debug("open: i-node=%ld\n", vp->v_ino);
    if (!vp->v_data) {
        vp->v_data = new ext_vdata();
        ext_debug("open: i-node=%ld allocating v_data\n", vp->v_ino);
    }
    ext_vdata *vdata = (ext_vdata*) vp->v_data;
    vdata->ref_count++;
    return (EOK);
}

static mutex_t inodes_to_delete_mutex;
static std::set<uint64_t> inodes_to_delete;

static void
set_inode_to_be_deleted(uint64_t inode)
{
    mutex_lock(&inodes_to_delete_mutex);
    inodes_to_delete.emplace(inode);
    mutex_unlock(&inodes_to_delete_mutex);
}

static void
remove_inode_from_being_deleted(uint64_t inode)
{
    mutex_lock(&inodes_to_delete_mutex);
    inodes_to_delete.erase(inode);
    mutex_unlock(&inodes_to_delete_mutex);
}

void ext_delete_outstanding_inodes(struct ext4_fs *fs)
{
    for (auto inode_no : inodes_to_delete) {
        auto_inode_ref inode(fs, inode_no);
        if (inode._r != EOK) {
            continue;
        }
        // Set a non-zero deletion time before freeing so Linux e2fsck does not
        // flag "deleted inode has zero dtime" (lwext4's own delete path does the
        // same with -1L).
        ext_mark_inode_deleted(inode._ref.inode);
        ext4_fs_free_inode(&inode._ref);
        ext_debug("delete: i-node=%ld when unmounting\n", inode_no);
    }
    inodes_to_delete.clear();
}

static int
ext_close(vnode_t *vp, file_t *fp)
{
    ext_debug("close: i-node %ld\n", vp->v_ino);
    if (!vp->v_data) {
        ext_debug("close: i-node %ld that has not been opened!\n", vp->v_ino);
        return (EOK);
    }

    ext_vdata *vdata = (ext_vdata*) vp->v_data;
    vdata->ref_count--;

    if (vdata->delete_on_last_close && vdata->ref_count == 0) {
        ext_debug("close: deleting i-node %ld on close\n", vp->v_ino);
        vdata->delete_on_last_close = false;

        struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
        auto_inode_ref inode(fs, vp->v_ino);
        if (inode._r != EOK) {
            return inode._r;
        }
        ext_mark_inode_deleted(inode._ref.inode);
        ext4_fs_free_inode(&inode._ref);

        remove_inode_from_being_deleted(vp->v_ino);
    }
    return (EOK);
}

static int
ext_internal_read(struct ext4_fs *fs, struct ext4_inode_ref *ref, uint64_t offset, void *buf, size_t size, size_t *rcnt)
{
    ext4_fsblk_t fblock;
    ext4_fsblk_t fblock_start;

    uint8_t *u8_buf = (uint8_t *)buf;
    int r;

    if (!size)
        return EOK;

    struct ext4_sblock *const sb = &fs->sb;

    if (rcnt)
        *rcnt = 0;

    /*Sync file size*/
    uint64_t fsize = ext4_inode_get_size(sb, ref->inode);

    uint32_t block_size = ext4_sb_get_block_size(sb);
    size = ((uint64_t)size > (fsize - offset))
        ? ((size_t)(fsize - offset)) : size;

    uint32_t iblock_idx = (uint32_t)((offset) / block_size);
    uint32_t iblock_last = (uint32_t)((offset + size) / block_size);
    uint32_t unalg = (offset) % block_size;

    uint32_t fblock_count = 0;
    if (unalg) {
        size_t len =  size;
        if (size > (block_size - unalg))
            len = block_size - unalg;

        r = ext4_fs_get_inode_dblk_idx(ref, iblock_idx, &fblock, true);
        if (r != EOK)
            goto Finish;

        /* Do we get an unwritten range? */
        if (fblock != 0) {
            uint64_t off = fblock * block_size + unalg;
            r = ext4_block_readbytes(fs->bdev, off, u8_buf, len);
            if (r != EOK)
                goto Finish;

        } else {
            /* Yes, we do. */
            memset(u8_buf, 0, len);
        }

        u8_buf += len;
        size -= len;
        offset += len;

        if (rcnt)
            *rcnt += len;

        iblock_idx++;
    }

    fblock_start = 0;
    while (size >= block_size) {
        while (iblock_idx < iblock_last) {
            r = ext4_fs_get_inode_dblk_idx(ref, iblock_idx,
                               &fblock, true);
            if (r != EOK)
                goto Finish;

            iblock_idx++;

            if (!fblock_start)
                fblock_start = fblock;

            if ((fblock_start + fblock_count) != fblock)
                break;

            fblock_count++;
        }

        ext_debug("ext4_blocks_get_direct: block_start:%ld, block_count:%d\n", fblock_start, fblock_count);
        r = ext4_blocks_get_direct(fs->bdev, u8_buf, fblock_start,
                       fblock_count);
        if (r != EOK)
            goto Finish;

        size -= block_size * fblock_count;
        u8_buf += block_size * fblock_count;
        offset += block_size * fblock_count;

        if (rcnt)
            *rcnt += block_size * fblock_count;

        fblock_start = fblock;
        fblock_count = 1;
    }

    if (size) {
        r = ext4_fs_get_inode_dblk_idx(ref, iblock_idx, &fblock, true);
        if (r != EOK)
            goto Finish;

        uint64_t off = fblock * block_size;
        ext_debug("ext4_block_readbytes: off:%ld, size:%ld\n", off, size);
        r = ext4_block_readbytes(fs->bdev, off, u8_buf, size);
        if (r != EOK)
            goto Finish;

        offset += size;

        if (rcnt)
            *rcnt += size;
    }

Finish:
    return r;
}

static const size_t RA_WINDOW = 1 << 20;   // 1 MiB read-ahead window

// Prefetch worker: one per open vnode, spawned lazily. Sleeps until the read
// path hands it a request (req_buf/req_off/req_len), fills the buffer with its
// own inode_ref (concurrent reads of the same inode are safe: lwext4 locks its
// block cache internally and inode refs are per-caller), then reports the
// result. This keeps the NVMe queue busy while the app consumes the current
// window.
static void *ext_prefetch_worker(void *arg)
{
    ext_vdata *vdata = (ext_vdata *)arg;
    pthread_mutex_lock(&vdata->ra_cvmtx);
    for (;;) {
        while (!vdata->req_work && !vdata->shutdown) {
            pthread_cond_wait(&vdata->ra_req_cv, &vdata->ra_cvmtx);
        }
        if (vdata->shutdown) {
            break;
        }
        // Take the request. Copy out under the handshake lock, then read
        // without holding it so the read path can keep serving from cur_buf.
        char *buf = vdata->req_buf;
        uint64_t off = vdata->req_off;
        size_t len = vdata->req_len;
        vdata->req_work = false;
        vdata->worker_busy = true;
        pthread_mutex_unlock(&vdata->ra_cvmtx);

        size_t got = 0;
        int err = EIO;
        struct ext4_fs *fs = vdata->fs;
        if (fs && buf) {
            auto_inode_ref ref(fs, vdata->inode_no);
            if (ref._r == EOK) {
                err = ext_internal_read(fs, &ref._ref, off, buf, len, &got);
            } else {
                err = ref._r;
            }
        }

        pthread_mutex_lock(&vdata->ra_cvmtx);
        vdata->req_got = got;
        vdata->req_err = err;
        vdata->worker_busy = false;
        pthread_cond_signal(&vdata->ra_done_cv);
    }
    pthread_mutex_unlock(&vdata->ra_cvmtx);
    return nullptr;
}

// Ask the worker to prefetch [off, off+len) into next_buf. Called with ra_lock
// held. Non-blocking: just posts the request and marks next_pending.
static void ext_kick_prefetch(ext_vdata *vdata, uint64_t off, size_t len)
{
    if (!vdata->next_buf) {
        vdata->next_buf = (char *)alloc_contiguous_aligned(
            RA_WINDOW, alignof(std::max_align_t));
        if (!vdata->next_buf) {
            return;
        }
    }
    pthread_mutex_lock(&vdata->ra_cvmtx);
    // Don't queue a new request while the worker is still busy with the last
    // one; the read path harvests completion before kicking again.
    if (vdata->worker_busy || vdata->req_work) {
        pthread_mutex_unlock(&vdata->ra_cvmtx);
        return;
    }
    vdata->req_buf = vdata->next_buf;
    vdata->req_off = off;
    vdata->req_len = len;
    vdata->req_work = true;
    vdata->next_start = off;
    vdata->next_len = 0;
    vdata->next_ready = false;
    vdata->next_pending = true;
    pthread_cond_signal(&vdata->ra_req_cv);
    pthread_mutex_unlock(&vdata->ra_cvmtx);
}

// Harvest a finished prefetch into next_ready/next_len. Called with ra_lock
// held. If block is true, wait for the worker to finish (used when the app
// needs the next window right now); otherwise just poll.
static void ext_reap_prefetch(ext_vdata *vdata, bool block)
{
    if (!vdata->next_pending) {
        return;
    }
    pthread_mutex_lock(&vdata->ra_cvmtx);
    while (block && (vdata->worker_busy || vdata->req_work)) {
        pthread_cond_wait(&vdata->ra_done_cv, &vdata->ra_cvmtx);
    }
    if (!vdata->worker_busy && !vdata->req_work) {
        // Completed.
        vdata->next_pending = false;
        if (vdata->req_err == EOK) {
            vdata->next_len = vdata->req_got;
            vdata->next_ready = true;
        } else {
            vdata->next_len = 0;
            vdata->next_ready = false;
        }
    }
    pthread_mutex_unlock(&vdata->ra_cvmtx);
}

// Cancel any in-flight prefetch and drop both windows. Called with ra_lock
// held on write/truncate invalidation. Blocks until the worker is idle so its
// buffer is not being written when we discard it.
static void ext_ra_invalidate(ext_vdata *vdata)
{
    if (vdata->next_pending) {
        pthread_mutex_lock(&vdata->ra_cvmtx);
        while (vdata->worker_busy || vdata->req_work) {
            pthread_cond_wait(&vdata->ra_done_cv, &vdata->ra_cvmtx);
        }
        pthread_mutex_unlock(&vdata->ra_cvmtx);
        vdata->next_pending = false;
    }
    vdata->cur_len = 0;
    vdata->cur_start = 0;
    vdata->next_len = 0;
    vdata->next_ready = false;
    vdata->ra_next = 0;
}

// Serve read_amt bytes at cur_start-relative offset `off` from cur_buf into the
// uio, then advance the uio. Caller guarantees the range is inside cur_buf.
static void ext_serve_from_cur(ext_vdata *vdata, uio_t *uio, uint64_t off,
                               uint64_t read_amt)
{
    memcpy(uio->uio_iov[0].iov_base,
           vdata->cur_buf + (off - vdata->cur_start), read_amt);
    uio->uio_iov[0].iov_base = (char *)uio->uio_iov[0].iov_base + read_amt;
    uio->uio_iov[0].iov_len -= read_amt;
    uio->uio_resid -= read_amt;
    uio->uio_offset += read_amt;
    vdata->ra_next = off + read_amt;
}

// Ensure the worker thread exists. Called with ra_lock held.
static bool ext_ensure_worker(ext_vdata *vdata, struct ext4_fs *fs,
                              uint32_t inode_no)
{
    if (vdata->worker_started) {
        return true;
    }
    vdata->fs = fs;
    vdata->inode_no = inode_no;
    if (pthread_create(&vdata->worker, nullptr, ext_prefetch_worker, vdata) == 0) {
        vdata->worker_started = true;
    }
    return vdata->worker_started;
}

static int
ext_read(vnode_t *vp, struct file *fp, uio_t *uio, int ioflag)
{
    ext_debug("read: %ld bytes at offset:%ld from file i-node=%ld\n", uio->uio_resid, uio->uio_offset, vp->v_ino);

    /* Cant read directories */
    if (vp->v_type == VDIR)
        return EISDIR;

    /* Cant read anything but reg */
    if (vp->v_type != VREG)
        return EINVAL;

    /* Cant start reading before the first byte */
    if (uio->uio_offset < 0)
        return EINVAL;

    /* Need to read more than 1 byte */
    if (uio->uio_resid == 0)
        return 0;

    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    // Total read amount is what they requested, or what is left
    uint64_t fsize = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    if ((uint64_t)uio->uio_offset >= fsize) {
        return 0;
    }
    uint64_t read_amt = std::min(fsize - uio->uio_offset, (uint64_t)uio->uio_resid);

    // ASYNC double-buffered sequential read-ahead. For a single-iovec read
    // smaller than half a window, serve from cur_buf (a memcpy) and keep the
    // device busy by prefetching the next window in the background. On a
    // sequential pattern this hides read latency behind the app's compute.
    ext_vdata *vdata = (ext_vdata *)vp->v_data;
    if (vdata && uio->uio_iovcnt == 1 && read_amt <= RA_WINDOW / 2) {
        uint64_t off = uio->uio_offset;
        mutex_lock(&vdata->ra_lock);

        // 1) Fully inside the current window -> serve it, then top up next.
        if (vdata->cur_buf && vdata->cur_len && off >= vdata->cur_start &&
            off + read_amt <= vdata->cur_start + vdata->cur_len) {
            ext_serve_from_cur(vdata, uio, off, read_amt);
            // If the app is near the end of cur, make sure a prefetch of the
            // window after next is in flight (kick once per window).
            uint64_t cur_end = vdata->cur_start + vdata->cur_len;
            if (vdata->ra_next >= vdata->cur_start + vdata->cur_len / 2 &&
                !vdata->next_pending && !vdata->next_ready &&
                cur_end < fsize) {
                size_t win = (size_t)std::min((uint64_t)RA_WINDOW, fsize - cur_end);
                if (ext_ensure_worker(vdata, fs, vp->v_ino)) {
                    ext_kick_prefetch(vdata, cur_end, win);
                }
            }
            mutex_unlock(&vdata->ra_lock);
            return 0;
        }

        // 2) Crossed out of cur. If a prefetched "next" window covers this
        //    offset, promote it to cur (waiting for it if still in flight),
        //    serve, and kick the following window.
        if (off == vdata->ra_next && vdata->next_pending) {
            ext_reap_prefetch(vdata, /*block=*/true);
        }
        if (vdata->next_ready && off >= vdata->next_start &&
            off + read_amt <= vdata->next_start + vdata->next_len) {
            // Swap next -> cur.
            std::swap(vdata->cur_buf, vdata->next_buf);
            vdata->cur_start = vdata->next_start;
            vdata->cur_len = vdata->next_len;
            vdata->next_ready = false;
            vdata->next_len = 0;
            ext_serve_from_cur(vdata, uio, off, read_amt);
            uint64_t cur_end = vdata->cur_start + vdata->cur_len;
            if (cur_end < fsize) {
                size_t win = (size_t)std::min((uint64_t)RA_WINDOW, fsize - cur_end);
                if (ext_ensure_worker(vdata, fs, vp->v_ino)) {
                    ext_kick_prefetch(vdata, cur_end, win);
                }
            }
            mutex_unlock(&vdata->ra_lock);
            return 0;
        }

        // 3) Cold miss / seek. Fill cur synchronously (as before) and, if this
        //    looks sequential, immediately kick prefetch of the next window.
        if (off == vdata->ra_next || vdata->cur_len == 0) {
            size_t win = (size_t)std::min((uint64_t)RA_WINDOW, fsize - off);
            if (!vdata->cur_buf) {
                vdata->cur_buf = (char *)alloc_contiguous_aligned(
                    RA_WINDOW, alignof(std::max_align_t));
            }
            if (vdata->cur_buf) {
                // Any stale in-flight prefetch is invalid after a seek.
                ext_reap_prefetch(vdata, /*block=*/true);
                vdata->next_ready = false;
                vdata->next_len = 0;
                size_t got = 0;
                int rr = ext_internal_read(fs, &inode_ref._ref, off,
                                           vdata->cur_buf, win, &got);
                if (rr == 0 && got >= read_amt) {
                    vdata->cur_start = off;
                    vdata->cur_len = got;
                    ext_serve_from_cur(vdata, uio, off, read_amt);
                    uint64_t cur_end = off + got;
                    if (cur_end < fsize) {
                        size_t win2 = (size_t)std::min((uint64_t)RA_WINDOW,
                                                       fsize - cur_end);
                        if (ext_ensure_worker(vdata, fs, vp->v_ino)) {
                            ext_kick_prefetch(vdata, cur_end, win2);
                        }
                    }
                    mutex_unlock(&vdata->ra_lock);
                    return 0;
                }
            }
        }
        vdata->ra_next = off + read_amt;   // track for next-time detection
        mutex_unlock(&vdata->ra_lock);
    }

    // Fast path: a single contiguous user iovec lets lwext4 read straight into
    // the caller's buffer, avoiding a full-size bounce allocation and the
    // extra uiomove() copy that dominated the old read path.
    if (uio->uio_iovcnt == 1 && uio->uio_iov[0].iov_len >= read_amt) {
        size_t read_count = 0;
        int ret = ext_internal_read(fs, &inode_ref._ref, uio->uio_offset,
                                    uio->uio_iov[0].iov_base, read_amt, &read_count);
        if (ret) {
            kprintf("[ext_read] Error reading data\n");
            return ret;
        }
        // Advance the uio to reflect what we consumed (mirrors uiomove).
        uio->uio_iov[0].iov_base = (char *)uio->uio_iov[0].iov_base + read_count;
        uio->uio_iov[0].iov_len -= read_count;
        uio->uio_resid -= read_count;
        uio->uio_offset += read_count;
        return 0;
    }

    // Slow path (scatter/gather or short iovec): bounce through a temp buffer.
    void *buf = alloc_contiguous_aligned(read_amt, alignof(std::max_align_t));

    size_t read_count = 0;
    int ret = ext_internal_read(fs, &inode_ref._ref, uio->uio_offset, buf, read_amt, &read_count);
    if (ret) {
        kprintf("[ext_read] Error reading data\n");
        free_contiguous_aligned(buf);
        return ret;
    }

    ret = uiomove(buf, read_count, uio);
    free_contiguous_aligned(buf);

    return ret;
}

static int
ext_internal_write(struct ext4_fs *fs, struct ext4_inode_ref *ref, uint64_t offset, void *buf, size_t size, size_t *wcnt)
{
    ext_debug("[ext4_internal_write] Writing %ld bytes at offset:%ld\n", size, offset);
    ext4_fsblk_t fblock;
    ext4_fsblk_t fblock_start = 0;

    uint8_t *u8_buf = (uint8_t *)buf;
    int r, rr = EOK;

    if (!size)
        return EOK;

    struct ext4_sblock *const sb = &fs->sb;

    if (wcnt)
        *wcnt = 0;

    /*Sync file size*/
    uint64_t fsize = ext4_inode_get_size(sb, ref->inode);
    uint32_t block_size = ext4_sb_get_block_size(sb);

    uint32_t iblock_last = (uint32_t)((offset + size) / block_size);
    uint32_t iblk_idx = (uint32_t)(offset / block_size);
    uint32_t ifile_blocks = (uint32_t)((fsize + block_size - 1) / block_size);

    uint32_t unalg = (offset) % block_size;

    uint32_t fblock_count = 0;

    if (unalg) {
        size_t len = size;
        uint64_t off;
        if (size > (block_size - unalg))
            len = block_size - unalg;

        if (iblk_idx < ifile_blocks) {
            r = ext4_fs_init_inode_dblk_idx(ref, iblk_idx, &fblock);
        }
        else {
            r = ext4_fs_append_inode_dblk(ref, &fblock, &iblk_idx);
            ext_debug("[ext_internal_write] Appended block=%d, phys:%ld\n", iblk_idx, fblock);
            ifile_blocks++;
        }
        if (r != EOK)
            goto Finish;

        off = fblock * block_size + unalg;
        r = ext4_block_writebytes(fs->bdev, off, u8_buf, len);
        ext_debug("[ext_internal_write] Wrote unaligned %ld bytes at %ld\n", len, off);
        if (r != EOK)
            goto Finish;

        u8_buf += len;
        size -= len;
        offset += len;

        if (wcnt)
            *wcnt += len;

        iblk_idx++;
    }

    //Sometimes file size is less than caller what to start writing at
    //For example, it is valid to lseek() with SEEK_END with offset to position
    //file for writing beyond its size.
    //On Linux, the ext4 supports it as a sparse file but our lwext4-based
    //implementation does not support sparse files really. So in such case,
    //we simply append as many missing blocks as needed to close the gap
    while (ifile_blocks < iblk_idx) {
        uint32_t iblk_idx2;
        auto res = ext4_fs_append_inode_dblk(ref, nullptr, &iblk_idx2);
        if (res != EOK) {
            offset = ifile_blocks * block_size;
            goto out_fsize;
        }
        ext_debug("[ext_internal_write] Appended (2) block=%d\n", iblk_idx2);
        ifile_blocks++;
    }

    while (size >= block_size) {

        while (iblk_idx < iblock_last) {
            if (iblk_idx < ifile_blocks) {
                r = ext4_fs_init_inode_dblk_idx(ref, iblk_idx,
                                &fblock);
                if (r != EOK)
                    goto Finish;
            } else {
                rr = ext4_fs_append_inode_dblk(ref, &fblock,
                                   &iblk_idx);
                ext_debug("[ext_internal_write] Appended (3) block=%d, phys:%ld\n", iblk_idx, fblock);
                if (rr != EOK) {
                    /* Unable to append more blocks. But
                     * some block might be allocated already
                     * */
                    break;
                }
            }

            iblk_idx++;

            if (!fblock_start) {
                fblock_start = fblock;
            }

            if ((fblock_start + fblock_count) != fblock)
                break;

            fblock_count++;
        }

        r = ext4_blocks_set_direct(fs->bdev, u8_buf, fblock_start,
                       fblock_count);
        ext_debug("[ext_internal_write] Wrote direct %d blocks at block %ld\n", fblock_count, fblock_start);
        if (r != EOK)
            break;

        size -= block_size * fblock_count;
        u8_buf += block_size * fblock_count;
        offset += block_size * fblock_count;

        if (wcnt)
            *wcnt += block_size * fblock_count;

        fblock_start = fblock;
        fblock_count = 1;

        if (rr != EOK) {
            /*ext4_fs_append_inode_block has failed and no
             * more blocks might be written. But node size
             * should be updated.*/
            r = rr;
            goto out_fsize;
        }
    }

    if (r != EOK)
        goto Finish;

    if (size) {
        uint64_t off;
        if (iblk_idx < ifile_blocks) {
            r = ext4_fs_init_inode_dblk_idx(ref, iblk_idx, &fblock);
            if (r != EOK)
                goto Finish;
        } else {
            r = ext4_fs_append_inode_dblk(ref, &fblock, &iblk_idx);
            ext_debug("[ext_internal_write] Appended (4) block=%d, phys:%ld\n", iblk_idx, fblock);
            if (r != EOK)
                /*Node size sholud be updated.*/
                goto out_fsize;
        }

        off = fblock * block_size;
        r = ext4_block_writebytes(fs->bdev, off, u8_buf, size);
        ext_debug("[ext_internal_write] Wrote remaining %ld bytes at %ld\n", size, off);
        if (r != EOK)
            goto Finish;

        offset += size;

        if (wcnt)
            *wcnt += size;
    }

out_fsize:
    if (offset > fsize) {
        ext4_inode_set_size(ref->inode, offset);
        ref->dirty = true;
    }

Finish:
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    bool extra_avail = ext4_get16(sb, inode_size) > EXT4_GOOD_OLD_INODE_SIZE;
    set_inode_time(ref->inode, now, change_inode, extra_avail);
    set_inode_time(ref->inode, now, modif, extra_avail);
    ref->dirty = true;

    return r;
}

static int
ext_write(vnode_t *vp, uio_t *uio, int ioflag)
{
    /* Cant write directories */
    if (vp->v_type == VDIR)
        return EISDIR;

    /* Cant write anything but reg */
    if (vp->v_type != VREG)
        return EINVAL;

    /* Cant start writing before the first byte */
    if (uio->uio_offset < 0)
        return EINVAL;

    if (uio->uio_offset >= LONG_MAX)
        return EFBIG;

    /* Need to write more than 1 byte */
    if (uio->uio_resid == 0)
        return 0;

    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    // A write invalidates any cached read-ahead window for this file, and must
    // cancel any in-flight prefetch before it can free/reuse those buffers.
    {
        ext_vdata *vdata = (ext_vdata *)vp->v_data;
        if (vdata) {
            mutex_lock(&vdata->ra_lock);
            ext_ra_invalidate(vdata);
            mutex_unlock(&vdata->ra_lock);
        }
    }

    if (ioflag & IO_APPEND) {
        uio->uio_offset = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    }

    ext_debug("write: %ld bytes at offset:%ld to file i-node=%ld\n", uio->uio_resid, uio->uio_offset, vp->v_ino);

    // Fast path: a single contiguous user iovec lets lwext4 write straight from
    // the caller's buffer, avoiding a full-size bounce allocation and the extra
    // uiomove() copy.
    if (uio->uio_iovcnt == 1 && (size_t)uio->uio_iov[0].iov_len >= (size_t)uio->uio_resid) {
        size_t want = uio->uio_resid;
        size_t write_count = 0;
        int ret = ext_internal_write(fs, &inode_ref._ref, uio->uio_offset,
                                     uio->uio_iov[0].iov_base, want, &write_count);
        uio->uio_iov[0].iov_base = (char *)uio->uio_iov[0].iov_base + write_count;
        uio->uio_iov[0].iov_len -= write_count;
        uio->uio_resid -= write_count;
        uio->uio_offset += write_count;
        vp->v_size = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
        return ret;
    }

    uio_t uio_copy = *uio;
    void *buf = alloc_contiguous_aligned(uio->uio_resid, alignof(std::max_align_t));
    int ret = uiomove(buf, uio->uio_resid, &uio_copy);
    if (ret) {
        kprintf("[ext_write] Error copying data\n");
        free_contiguous_aligned(buf);
        return ret;
    }

    size_t write_count = 0;
    ret = ext_internal_write(fs, &inode_ref._ref, uio->uio_offset, buf, uio->uio_resid, &write_count);

    uio->uio_resid -= write_count;
    free_contiguous_aligned(buf);

    vp->v_size = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);

    return ret;
}

static int
ext_ioctl(vnode_t *vp, file_t *fp, u_long com, void *data)
{
    ext_debug("ioctl\n");
    return (EINVAL);
}

static int
ext_fsync(struct vnode *vp, struct file *fp)
{
    // lwext4 mounts with block-cache write-back enabled (ext_vfsops.cc), so a
    // write only reaches the disk when the block cache is flushed.  fsync(2)/
    // fdatasync(2) must make the file's data durable, so flush the device's
    // block cache here (the cache is shared per device, so this persists this
    // file's dirty buffers along with any others -- correct, if slightly more
    // than the minimum a per-inode flush would do).
    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    fs->bcache_lock();
    int r = ext4_block_cache_flush(fs->bdev);
    fs->bcache_unlock();
    return r;
}

static int
ext_readdir(struct vnode *dvp, struct file *fp, struct dirent *dir)
{
#define EXT4_DIR_ENTRY_OFFSET_TERM (uint64_t)(-1)
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    struct ext4_inode_ref inode_ref;

    if (file_offset(fp) == 1) {//EXT4_DIR_ENTRY_OFFSET_TERM) {
        return ENOENT;
    }

    int r = ext4_fs_get_inode_ref(fs, dvp->v_ino, &inode_ref);
    if (r != EOK) {
        return r;
    }

    /* Check if node is directory */
    if (!ext4_inode_is_type(&fs->sb, inode_ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        ext4_fs_put_inode_ref(&inode_ref);
        ext_debug("readdir: i-node %li not a directory\n", dvp->v_ino);
        return ENOTDIR;
    }

    ext_debug("readdir directory with i-node=%ld at offset:%ld\n", dvp->v_ino, file_offset(fp));
    struct ext4_dir_iter it;
    int rc = ext4_dir_iterator_init(&it, &inode_ref, file_offset(fp));
    if (rc != EOK) {
        kprintf("[ext4] Reading directory with i-node=%ld at offset:%ld -> FAILED to init iterator\n", dvp->v_ino, file_offset(fp));
        ext4_fs_put_inode_ref(&inode_ref);
        return rc;
    }

    /* Test for non-empty directory entry */
    if (it.curr != NULL) {
        if (ext4_dir_en_get_inode(it.curr) != 0) {
            memset(dir->d_name, 0, sizeof(dir->d_name));
            uint16_t name_length = ext4_dir_en_get_name_len(&fs->sb, it.curr);
            memcpy(dir->d_name, it.curr->name, name_length);
            ext_debug("readdir directory with i-node=%ld at offset:%ld => entry name:%s\n", dvp->v_ino, file_offset(fp), dir->d_name);

            dir->d_ino = ext4_dir_en_get_inode(it.curr);

            uint8_t i_type = ext4_dir_en_get_inode_type(&fs->sb, it.curr);
            if (i_type == EXT4_DE_DIR) {
                dir->d_type = DT_DIR;
            } else if (i_type == EXT4_DE_REG_FILE) {
                dir->d_type = DT_REG;
            } else if (i_type == EXT4_DE_SYMLINK) {
                dir->d_type = DT_LNK;
            }

            ext4_dir_iterator_next(&it);

            off_t f_offset = file_offset(fp);
            dir->d_fileno = f_offset;
            dir->d_off = f_offset + 1;
            file_setoffset(fp, it.curr ? it.curr_off : EXT4_DIR_ENTRY_OFFSET_TERM);
        } else {
            ext_debug("readdir directory with i-node=%ld at offset:%ld -> cos ni tak\n", dvp->v_ino, file_offset(fp));
        }
    } else {
        ext4_dir_iterator_fini(&it);
        ext4_fs_put_inode_ref(&inode_ref);
        ext_debug("readdir directory with i-node=%ld at offset:%ld -> ENOENT\n", dvp->v_ino, file_offset(fp));
        return ENOENT;
    }

    rc = ext4_dir_iterator_fini(&it);
    ext4_fs_put_inode_ref(&inode_ref);
    if (rc != EOK)
        return rc;

    return EOK;
}

static int
ext_lookup(struct vnode *dvp, char *nm, struct vnode **vpp)
{
    ext_debug("lookup %s in directory with i-node=%ld\n", nm, dvp->v_ino);
    *vpp = nullptr;
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;

    auto_inode_ref inode_ref(fs, dvp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    /* Check if node is directory */
    if (!ext4_inode_is_type(&fs->sb, inode_ref._ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        ext_debug("lookup: i-node %li not a directory\n", dvp->v_ino);
        return ENOTDIR;
    }

    struct ext4_dir_search_result result;
    int r = ext4_dir_find_entry(&result, &inode_ref._ref, nm, strlen(nm));
    if (r == EOK) {
        uint32_t inode_no = ext4_dir_en_get_inode(result.dentry);
        struct vnode *vp;
        if (vget(dvp->v_mount, inode_no, &vp)) {
            ext_debug("lookup: i-node %i found in cache\n", inode_no);
            *vpp = vp;
            return EOK;
        }

        auto_inode_ref inode_ref2(fs, inode_no);
        if (inode_ref2._r != EOK) {
            return inode_ref2._r;
        }

        uint32_t i_type = ext4_inode_type(&fs->sb, inode_ref2._ref.inode);
        if (i_type == EXT4_INODE_MODE_DIRECTORY) {
            vp->v_type = VDIR;
        } else if (i_type == EXT4_INODE_MODE_FILE) {
            vp->v_type = VREG;
            vp->v_size = ext4_inode_get_size(&fs->sb, inode_ref2._ref.inode);
        } else if (i_type == EXT4_INODE_MODE_SOFTLINK) {
            vp->v_type = VLNK;
        }

        vp->v_mode = ext4_inode_get_mode(&fs->sb, inode_ref2._ref.inode);
        vp->v_data = new ext_vdata();

        ext_debug("lookup: found %s %s in directory with i-node=%ld as i-node=%d with size:%ld\n",
            vp->v_type == VDIR ? "DIR" : (vp->v_type == VREG ? "FILE" : "SYMLINK"),
            nm, dvp->v_ino, inode_no, vp->v_size);
        *vpp = vp;
    } else {
        r = ENOENT;
    }

    ext4_dir_destroy_result(&inode_ref._ref, &result);

    return r;
}

static int
ext_dir_initialize(ext4_inode_ref *parent, ext4_inode_ref *child, bool dir_index_on)
{
    int r;
#if CONFIG_DIR_INDEX_ENABLE
    /* Initialize directory index if supported */
    if (dir_index_on) {
        ext_debug("dir_initialize: DIR_INDEX on initializing directory with inode no:%d\n", child->index);
        r = ext4_dir_dx_init(child, parent);
        if (r != EOK)
            return r;

        ext4_inode_set_flag(child->inode, EXT4_INODE_FLAG_INDEX);
    } else
#endif
    {
        r = ext4_dir_add_entry(child, ".", strlen("."), child);
        if (r != EOK) {
            return r;
        }

        r = ext4_dir_add_entry(child, "..", strlen(".."), parent);
        if (r != EOK) {
            ext4_dir_remove_entry(child, ".", strlen("."));
            return r;
        }
    }

    /*New empty directory. Two links (. and ..) */
    ext4_inode_set_links_cnt(child->inode, 2);
    ext4_fs_inode_links_count_inc(parent);
    parent->dirty = true;
    child->dirty = true;

    return r;
}

static int
ext_dir_link(struct vnode *dvp, char *name, int file_type, mode_t mode, uint32_t *inode_no, uint32_t *inode_no_created)
{
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    auto_inode_ref inode_ref(fs, dvp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    /* Check if node is directory */
    if (!ext4_inode_is_type(&fs->sb, inode_ref._ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        ext_debug("dir_link: i-node=%li not a directory\n", dvp->v_ino);
        return ENOTDIR;
    }

    struct ext4_dir_search_result result;
    int r = ext4_dir_find_entry(&result, &inode_ref._ref, name, strlen(name));
    ext4_dir_destroy_result(&inode_ref._ref, &result);
    if (r == EOK) {
        ext_debug("dir_link: %s already exists under i-node=%li\n", name, dvp->v_ino);
        return EEXIST;
    }

    struct ext4_inode_ref child_ref;
    if (inode_no) {
        r = ext4_fs_get_inode_ref(fs, *inode_no, &child_ref);
    } else {
        r = ext4_fs_alloc_inode(fs, &child_ref, file_type);
    }
    if (r != EOK) {
        return r;
    }

    if (!inode_no ) {
        ext_debug("dir_link: i-node type for %s is %x\n", name, ext4_inode_type(&fs->sb, child_ref.inode));
        ext4_fs_inode_blocks_init(fs, &child_ref);
    }

    r = ext4_dir_add_entry(&inode_ref._ref, name, strlen(name), &child_ref);
    if (r == EOK) {
        bool is_dir = ext4_inode_is_type(&fs->sb, child_ref.inode, EXT4_INODE_MODE_DIRECTORY);
        if (is_dir && inode_no) {
            r = EPERM; //Cannot create hard links for directories
        } else if (is_dir) {
#if CONFIG_DIR_INDEX_ENABLE
            bool dir_index_on = ext4_sb_feature_com(&fs->sb, EXT4_FCOM_DIR_INDEX);
#else
            bool dir_index_on = false;
#endif
            ext_debug("dir_link: initializing directory %s with i-node=%d\n", name, child_ref.index);
            r = ext_dir_initialize(&inode_ref._ref, &child_ref, dir_index_on);
            if (r != EOK) {
                ext4_dir_remove_entry(&inode_ref._ref, name, strlen(name));
            }
        } else {
            ext4_fs_inode_links_count_inc(&child_ref);
        }
    }

    if (r == EOK) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        bool extra_avail = ext4_get16(&fs->sb, inode_size) > EXT4_GOOD_OLD_INODE_SIZE;
        set_inode_time(child_ref.inode, now, change_inode, extra_avail);
        if (!inode_no) {
            set_inode_time(child_ref.inode, now, access, extra_avail);
            set_inode_time(child_ref.inode, now, modif, extra_avail);

            if (mode) {
                uint32_t current_mode = ext4_inode_get_mode(&fs->sb, child_ref.inode);
                uint32_t inode_type = current_mode & EXT4_INODE_MODE_TYPE_MASK;
                uint32_t mode_to_set = (mode & ~EXT4_INODE_MODE_TYPE_MASK) | inode_type;
                ext_debug("dir_link: set mode of %s to:%x\n", name, mode_to_set);
                ext4_inode_set_mode(&fs->sb, child_ref.inode, mode_to_set);
            }
        }

        set_inode_time(inode_ref._ref.inode, now, change_inode, extra_avail);
        set_inode_time(inode_ref._ref.inode, now, modif, extra_avail);

        inode_ref._ref.dirty = true;
        child_ref.dirty = true;
        if (inode_no_created) {
            *inode_no_created = child_ref.index;
        }
        ext_debug("dir_link: created %s under i-node=%li with filetype:%d\n", name, dvp->v_ino, file_type);
    } else {
        if (!inode_no) {
            ext_mark_inode_deleted(child_ref.inode);
            ext4_fs_free_inode(&child_ref);
        }
        //We do not want to write new inode. But block has to be released.
        kprintf("[ext4] dir_link: failed to create %s under i-node=%li due to error:%d!\n", name, dvp->v_ino, r);
        child_ref.dirty = false;
    }

    ext4_fs_put_inode_ref(&child_ref);

    return r;
}

static int
ext_create(struct vnode *dvp, char *name, mode_t mode)
{
    ext_debug("create %s under i-node=%li\n", name, dvp->v_ino);

    uint32_t len = strlen(name);
    if (len > NAME_MAX || len > EXT4_DIRECTORY_FILENAME_LEN) {
        return ENAMETOOLONG;
    }

    if (!S_ISREG(mode))
        return EINVAL;

    return ext_dir_link(dvp, name, EXT4_DE_REG_FILE, mode, nullptr, nullptr);
}

static int
ext_trunc_inode(struct ext4_fs *fs, uint32_t index, uint64_t new_size, bool *update_cmtimes)
{
    struct ext4_inode_ref inode_ref;
    int r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
    if (r != EOK)
        return r;

    uint64_t inode_size = ext4_inode_get_size(&fs->sb, inode_ref.inode);
    ext4_fs_put_inode_ref(&inode_ref);

    if (new_size > inode_size) {
        //Expand size
        //TODO: This is rather very naive implementation which eagerly allocates
        //needed blocks and fills them with zero-es even if they may never be
        //written to by the client. Ideally we could either support sparse files
        //or implement some other trick when we reserve number of blocks and then
        //read zeros if user reads relevant area of the file
        size_t extra_size = new_size - inode_size;
        void *buf = alloc_contiguous_aligned(extra_size, alignof(std::max_align_t));
        memset(buf, 0, extra_size);
        size_t write_count = 0;

        auto_inode_ref inode_ref2(fs, index);
        if (inode_ref2._r != EOK) {
            return inode_ref2._r;
        }

        ext_debug("trunc_inode: expanding size of the node %d by %ld bytes\n", index, extra_size);
        r = ext_internal_write(fs, &inode_ref2._ref, inode_size, buf, extra_size, &write_count);
        free_contiguous_aligned(buf);
        return r;
    }

    bool truncated = false;
    while (inode_size > new_size + CONFIG_MAX_TRUNCATE_SIZE) {
        inode_size -= CONFIG_MAX_TRUNCATE_SIZE;

        r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
        if (r != EOK) {
            break;
        }
        r = ext4_fs_truncate_inode(&inode_ref, inode_size);
        if (r != EOK)
            ext4_fs_put_inode_ref(&inode_ref);
        else
            r = ext4_fs_put_inode_ref(&inode_ref);

        if (r != EOK) {
            goto Finish;
        }
        truncated = true;
    }

    if (inode_size > new_size) {
        inode_size = new_size;

        r = ext4_fs_get_inode_ref(fs, index, &inode_ref);
        if (r != EOK) {
            goto Finish;
        }
        r = ext4_fs_truncate_inode(&inode_ref, inode_size);
        if (r != EOK)
            ext4_fs_put_inode_ref(&inode_ref);
        else {
            r = ext4_fs_put_inode_ref(&inode_ref);
            truncated = true;
        }
    }

Finish:
    if (update_cmtimes) {
        *update_cmtimes = truncated;
    }
    return r;
}

static int
ext_dir_trunc(struct ext4_fs *fs, struct ext4_inode_ref *parent, struct ext4_inode_ref *dir)
{
    int r = EOK;
    uint32_t block_size = ext4_sb_get_block_size(&fs->sb);

#if CONFIG_DIR_INDEX_ENABLE
    /* Initialize directory index if supported */
    if (ext4_sb_feature_com(&fs->sb, EXT4_FCOM_DIR_INDEX)) {
        r = ext4_dir_dx_init(dir, parent);
        if (r != EOK)
            return r;

        r = ext_trunc_inode(fs, dir->index, EXT4_DIR_DX_INIT_BCNT * block_size, nullptr);
        if (r != EOK)
            return r;
    } else
#endif
    {
        r = ext_trunc_inode(fs, dir->index, block_size, nullptr);
        if (r != EOK)
            return r;
    }

    return ext4_fs_truncate_inode(dir, 0);
}

static int
ext_dir_remove_entry(struct vnode *dvp, struct vnode *vp, char *name)
{
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    auto_inode_ref parent(fs, dvp->v_ino);
    if (parent._r != EOK) {
        return parent._r;
    }

    auto_inode_ref child(fs, vp->v_ino);
    if (child._r != EOK) {
        return child._r;
    }

    ext_debug("dir_remove_entry: removing %s\n", name);

    int r = EOK;
    uint32_t inode_type = ext4_inode_type(&fs->sb, child._ref.inode);
    if (inode_type != EXT4_INODE_MODE_DIRECTORY) {
        if (ext4_inode_get_links_cnt(child._ref.inode) == 1) {
            r = ext_trunc_inode(fs, child._ref.index, 0, nullptr);
            if (r != EOK) {
                return r;
            }
        }
    } else {
        r = ext_dir_trunc(fs, &parent._ref, &child._ref);
        if (r != EOK) {
            return r;
        }
    }

    /* Remove entry from parent directory */
    r = ext4_dir_remove_entry(&parent._ref, name, strlen(name));
    if (r != EOK) {
        return r;
    }

    ext_vdata *vdata = (ext_vdata*) vp->v_data;
    assert(vdata);
    if (inode_type != EXT4_INODE_MODE_DIRECTORY) {
        int links_cnt = ext4_inode_get_links_cnt(child._ref.inode);
        if (links_cnt) {
            ext4_fs_inode_links_count_dec(&child._ref);
            child._ref.dirty = true;

            if (links_cnt == 1) {//Zero now
                if (vdata->ref_count == 0) {
                    ext_mark_inode_deleted(child._ref.inode);
                    ext4_fs_free_inode(&child._ref);
                } else {
                    ext_debug("dir_remove_entry: should remove i-node=%ld of %s on last close\n", vp->v_ino, name);
                    vdata->delete_on_last_close = true;
                    set_inode_to_be_deleted(vp->v_ino);
                }
            }
        }
    } else {
        if (vdata->ref_count == 0) {
            ext_mark_inode_deleted(child._ref.inode);
            ext4_fs_free_inode(&child._ref);
        } else {
            ext_debug("dir_remove_entry: should remove i-node=%ld of %s on last close\n", vp->v_ino, name);
            vdata->delete_on_last_close = true;
            set_inode_to_be_deleted(vp->v_ino);
        }
    }

    if (r == EOK) {
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        bool extra_avail = ext4_get16(&fs->sb, inode_size) > EXT4_GOOD_OLD_INODE_SIZE;
        set_inode_time(parent._ref.inode, now, change_inode, extra_avail);
        set_inode_time(parent._ref.inode, now, modif, extra_avail);

        parent._ref.dirty = true;
    }

    return r;
}

static int
ext_remove(struct vnode *dvp, struct vnode *vp, char *name)
{
    ext_debug("remove\n");
    return ext_dir_remove_entry(dvp, vp, name);
}

static int
ext_rename(struct vnode *sdvp, struct vnode *svp, char *snm,
           struct vnode *tdvp, struct vnode *tvp, char *tnm)
{
    ext_debug("rename\n");
    struct ext4_fs *fs = (struct ext4_fs *)sdvp->v_mount->m_data;

    int r = EOK;
    if (tvp) {
        // Remove destination file, first ... if exists
        ext_debug("rename removing %s from the target directory\n", tnm);
        auto_inode_ref target_dir(fs, tdvp->v_ino);
        if (target_dir._r != EOK) {
            return target_dir._r;
        }
        /* Remove entry from target directory */
        r = ext4_dir_remove_entry(&target_dir._ref, tnm, strlen(tnm));
        if (r != EOK) {
            return r;
        }
    }

    auto_inode_ref src_dir(fs, sdvp->v_ino);
    if (src_dir._r != EOK) {
        return src_dir._r;
    }

    auto_inode_ref src_entry(fs, svp->v_ino);
    if (src_entry._r != EOK) {
        return src_entry._r;
    }

    /* Same directory ? */
    if (sdvp == tdvp) {
        // Add new entry to the same directory
        r = ext4_dir_add_entry(&src_dir._ref, tnm, strlen(tnm), &src_entry._ref);
        if (r != EOK) {
            return r;
        }
    } else {
        // Add new entry to the destination directory
        auto_inode_ref dest_dir(fs, tdvp->v_ino);
        if (dest_dir._r != EOK) {
            return dest_dir._r;
        }

        r = ext4_dir_add_entry(&dest_dir._ref, tnm, strlen(tnm), &src_entry._ref);
        if (r != EOK) {
            return r;
        }
    }

    // If directory need to reposition '..' to different parent - target directory
    if (ext4_inode_is_type(&fs->sb, src_entry._ref.inode, EXT4_INODE_MODE_DIRECTORY)) {
        auto_inode_ref dest_dir(fs, tdvp->v_ino);
        if (dest_dir._r != EOK) {
            return dest_dir._r;
        }

        bool idx;
        idx = ext4_inode_has_flag(src_entry._ref.inode, EXT4_INODE_FLAG_INDEX);
        struct ext4_dir_search_result res;
        if (!idx) {
            r = ext4_dir_find_entry(&res, &src_entry._ref, "..", strlen(".."));
            if (r != EOK)
                return EIO;

            ext4_dir_en_set_inode(res.dentry, dest_dir._ref.index);
            ext4_trans_set_block_dirty(res.block.buf);
            r = ext4_dir_destroy_result(&src_entry._ref, &res);
            if (r != EOK)
                return r;

        } else {
#if CONFIG_DIR_INDEX_ENABLE
            r = ext4_dir_dx_reset_parent_inode(&src_entry._ref, dest_dir._ref.index);
            if (r != EOK)
                return r;

#endif
        }

        ext4_fs_inode_links_count_inc(&dest_dir._ref);
        dest_dir._ref.dirty = true;
    }

    /* Remove old entry from the source directory */
    r = ext4_dir_remove_entry(&src_dir._ref, snm, strlen(snm));
    if (r != EOK) {
        return r;
    }

    return r;
}

static int
ext_mkdir(struct vnode *dvp, char *dirname, mode_t mode)
{
    ext_debug("mkdir %s under i-node=%li\n", dirname, dvp->v_ino);

    uint32_t len = strlen(dirname);
    if (len > NAME_MAX || len > EXT4_DIRECTORY_FILENAME_LEN) {
        return ENAMETOOLONG;
    }

    if (!S_ISDIR(mode))
        return EINVAL;

    return ext_dir_link(dvp, dirname, EXT4_DE_DIR, mode, nullptr, nullptr);
}

static int
ext_rmdir(vnode_t *dvp, vnode_t *vp, char *name)
{
    ext_debug("rmdir %s\n", name);
    return ext_dir_remove_entry(dvp, vp, name);
}

static int
ext_getattr(vnode_t *vp, vattr_t *vap)
{
    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;

    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    vap->va_mode = ext4_inode_get_mode(&fs->sb, inode_ref._ref.inode) & ~EXT4_INODE_MODE_TYPE_MASK;

    uint32_t i_type = ext4_inode_type(&fs->sb, inode_ref._ref.inode);
    if (i_type == EXT4_INODE_MODE_DIRECTORY) {
       vap->va_type = VDIR;
    } else if (i_type == EXT4_INODE_MODE_FILE) {
        vap->va_type = VREG;
    } else if (i_type == EXT4_INODE_MODE_SOFTLINK) {
        vap->va_type = VLNK;
    }

    vap->va_nodeid = vp->v_ino;
    vap->va_size = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    vap->va_nlink = ext4_inode_get_links_cnt(inode_ref._ref.inode);
    ext_debug("getattr: i-node=%ld va_mode:%o, va_size:%ld\n", vp->v_ino, vap->va_mode, vap->va_size);

    bool extra_avail = ext4_get16(&fs->sb, inode_size) > EXT4_GOOD_OLD_INODE_SIZE;
    get_inode_time(inode_ref._ref.inode, vap->va_atime, access, extra_avail);
    get_inode_time(inode_ref._ref.inode, vap->va_mtime, modif, extra_avail);
    get_inode_time(inode_ref._ref.inode, vap->va_ctime, change_inode, extra_avail);

    //auto *fsid = &vnode->v_mount->m_fsid; //TODO
    //attr->va_fsid = ((uint32_t)fsid->__val[0]) | ((dev_t) ((uint32_t)fsid->__val[1]) << 32);

    return (EOK);
}

static int
ext_setattr(vnode_t *vp, vattr_t *vap)
{
    ext_debug("setattr: inode:%ld\n", vp->v_ino);
    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;

    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    bool extra_avail = ext4_get16(&fs->sb, inode_size) > EXT4_GOOD_OLD_INODE_SIZE;
    if (vap->va_mask & AT_ATIME) {
        set_inode_time(inode_ref._ref.inode, vap->va_atime, access, extra_avail);
        inode_ref._ref.dirty = true;
    }

    if (vap->va_mask & AT_CTIME) {
        set_inode_time(inode_ref._ref.inode, vap->va_ctime, change_inode, extra_avail);
        inode_ref._ref.dirty = true;
    }

    if (vap->va_mask & AT_MTIME) {
        set_inode_time(inode_ref._ref.inode, vap->va_mtime, modif, extra_avail);
        inode_ref._ref.dirty = true;
    }

    if (vap->va_mask & AT_MODE) {
        uint32_t mode = ext4_inode_get_mode(&fs->sb, inode_ref._ref.inode);
        uint32_t inode_type = mode & EXT4_INODE_MODE_TYPE_MASK;
        uint32_t mode_to_set = (vap->va_mode & ~EXT4_INODE_MODE_TYPE_MASK) | inode_type;
        ext_debug("setattr: i-node=%ld AT_MODE, va_mode:%o, mode to set:%x\n", vp->v_ino, vap->va_mode, mode_to_set);
        ext4_inode_set_mode(&fs->sb, inode_ref._ref.inode, mode_to_set);
        inode_ref._ref.dirty = true;
    }

    return (EOK);
}

static int
ext_truncate(struct vnode *vp, off_t new_size)
{
    ext_debug("truncate i-node=%ld, new_size:%ld\n", vp->v_ino, new_size);
    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    // Truncation changes the file's block layout; drop any read-ahead windows
    // and cancel in-flight prefetch first.
    {
        ext_vdata *vdata = (ext_vdata *)vp->v_data;
        if (vdata) {
            mutex_lock(&vdata->ra_lock);
            ext_ra_invalidate(vdata);
            mutex_unlock(&vdata->ra_lock);
        }
    }
    bool update_cmtimed = false;
    int r = ext_trunc_inode(fs, vp->v_ino, new_size, &update_cmtimed);
    if (update_cmtimed) {
        auto_inode_ref inode_ref(fs, vp->v_ino);
        if (inode_ref._r != EOK) {
            return inode_ref._r;
        }
        struct timespec now;
        clock_gettime(CLOCK_REALTIME, &now);
        bool extra_avail = ext4_get16(&fs->sb, inode_size) > EXT4_GOOD_OLD_INODE_SIZE;
        set_inode_time(inode_ref._ref.inode, now, change_inode, extra_avail);
        set_inode_time(inode_ref._ref.inode, now, modif, extra_avail);
        inode_ref._ref.dirty = true;
    }
    return r;
}

static int
ext_link(vnode_t *tdvp, vnode_t *svp, char *name)
{
    ext_debug("link: %s\n", name);
    uint32_t len = strlen(name);
    if (len > NAME_MAX || len > EXT4_DIRECTORY_FILENAME_LEN) {
        return ENAMETOOLONG;
    }

    uint32_t source_link_no = svp->v_ino;
    return ext_dir_link(tdvp, name, EXT4_DE_REG_FILE, 0, &source_link_no, nullptr);
}

static int
ext_fallocate(vnode_t *vp, int mode, loff_t offset, loff_t len)
{
    kprintf("[ext4] fallocate\n");
    return (EINVAL);
}

static int
ext_readlink(vnode_t *vp, uio_t *uio)
{
    ext_debug("readlink: i-node=%ld\n", vp->v_ino);
    if (vp->v_type != VLNK) {
        return EINVAL;
    }
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    if (uio->uio_resid == 0) {
        return 0;
    }

    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;

    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    uint64_t fsize = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    if (fsize < sizeof(inode_ref._ref.inode->blocks)
             && !ext4_inode_get_blocks_count(&fs->sb, inode_ref._ref.inode)) {

        char *content = (char *)inode_ref._ref.inode->blocks;
        return uiomove(content, fsize, uio);
    } else {
        uint32_t block_size = ext4_sb_get_block_size(&fs->sb);
        void *buf = malloc(block_size);
        size_t read_count = 0;
        int ret = ext_internal_read(fs, &inode_ref._ref, uio->uio_offset, buf, fsize, &read_count);
        if (ret) {
            kprintf("[ext_readlink] Error reading data\n");
            free(buf);
            return ret;
        }

        ret = uiomove(buf, read_count, uio);
        free(buf);
        return ret;
    }
}

static int
ext_fsymlink_set(struct ext4_fs *fs, uint32_t inode_no, const void *buf, uint32_t size)
{
    uint32_t block_size = ext4_sb_get_block_size(&fs->sb);
    if (size > block_size) {
        return EINVAL;
    }

    auto_inode_ref inode_ref(fs, inode_no);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    /*If the size of symlink is smaller than 60 bytes*/
    if (size < sizeof(inode_ref._ref.inode->blocks)) {
        memset(inode_ref._ref.inode->blocks, 0, sizeof(inode_ref._ref.inode->blocks));
        memcpy(inode_ref._ref.inode->blocks, buf, size);
        ext4_inode_clear_flag(inode_ref._ref.inode, EXT4_INODE_FLAG_EXTENTS);
    } else {
        ext4_fs_inode_blocks_init(fs, &inode_ref._ref);

        uint32_t sblock;
        ext4_fsblk_t fblock;
        int r = ext4_fs_append_inode_dblk(&inode_ref._ref, &fblock, &sblock);
        if (r != EOK)
            return r;

        uint64_t off = fblock * block_size;
        r = ext4_block_writebytes(fs->bdev, off, buf, size);
        if (r != EOK)
            return r;
    }

    ext4_inode_set_size(inode_ref._ref.inode, size);
    inode_ref._ref.dirty = true;

    return EOK;
}

static int
ext_symlink(vnode_t *dvp, char *name, char *link)
{
    ext_debug("symlink: name=%s, link:%s\n", name, link);
    uint32_t len = strlen(name);
    if (len > NAME_MAX || len > EXT4_DIRECTORY_FILENAME_LEN) {
        return ENAMETOOLONG;
    }
    struct ext4_fs *fs = (struct ext4_fs *)dvp->v_mount->m_data;
    uint32_t inode_no_created;
    int r = ext_dir_link(dvp, name, EXT4_DE_SYMLINK, 0, nullptr, &inode_no_created);
    if (r == EOK ) {
       return ext_fsymlink_set(fs, inode_no_created, link, strlen(link));
    }
    return r;
}

static int
ext_inactive(vnode_t *vp)
{
    if (vp->v_data) {
        ext_debug("inactive i-node=%ld with ref_count=%d\n", vp->v_ino, ((ext_vdata*)vp->v_data)->ref_count);
        delete (ext_vdata*)vp->v_data;
        vp->v_data = nullptr;
    } else {
        ext_debug("inactive i-node=%ld with NO v_data\n", vp->v_ino);
    }
    return EOK;
}

// vop_cache: warm the shared page cache with one page of file data so mmap
// faults (and readahead) on ext files can be served from the page cache like
// ROFS and ZFS, rather than each fault re-reading through the block layer.
//
// The uio's iov_base carries the pagecache hashkey; we read exactly one
// page-sized, page-aligned chunk at uio_offset into a freshly allocated page
// and hand it to pagecache::map_read_cached_page(), which takes ownership.
//
// ponytail: allocate-and-copy from lwext4, not a zero-copy borrow-and-pin.
// lwext4's block cache buffers are not page-aligned/shareable the way ROFS's
// read-around cache is, so copying is the correct first step; a borrow-and-pin
// bridge like the ZFS ARC one can come later if it shows up hot.
static int
ext_map_cached_page(struct vnode *vp, struct file *fp, struct uio *uio)
{
    if (vp->v_type == VDIR)
        return EISDIR;
    if (vp->v_type != VREG)
        return EINVAL;
    if (uio->uio_offset < 0)
        return EINVAL;
    if (uio->uio_offset >= (off_t)vp->v_size)
        return 0;
    if (uio->uio_resid != EXT_PAGE_SIZE)
        return EINVAL;
    if (uio->uio_offset % EXT_PAGE_SIZE)
        return EINVAL;

    struct ext4_fs *fs = (struct ext4_fs *)vp->v_mount->m_data;
    auto_inode_ref inode_ref(fs, vp->v_ino);
    if (inode_ref._r != EOK) {
        return inode_ref._r;
    }

    void *page = memory::alloc_page();
    if (!page) {
        return ENOMEM;
    }
    // Read up to a page; if the file's last page is short, the tail stays zero
    // (alloc_page does not zero, so zero it first to avoid leaking stale data).
    memset(page, 0, EXT_PAGE_SIZE);
    uint64_t fsize = ext4_inode_get_size(&fs->sb, inode_ref._ref.inode);
    size_t want = std::min((uint64_t)EXT_PAGE_SIZE,
                           fsize - (uint64_t)uio->uio_offset);
    size_t got = 0;
    int ret = ext_internal_read(fs, &inode_ref._ref, uio->uio_offset, page,
                                want, &got);
    if (ret) {
        memory::free_page(page);
        return ret;
    }
    pagecache::map_read_cached_page((pagecache::hashkey *)uio->uio_iov->iov_base,
                                    page);
    uio->uio_resid = 0;
    return 0;
}

#define ext_seek        ((vnop_seek_t)vop_nullop)

struct vnops ext_vnops = {
    ext_open,       /* open */
    ext_close,      /* close */
    ext_read,       /* read */
    ext_write,      /* write */
    ext_seek,       /* seek */
    ext_ioctl,      /* ioctl */
    ext_fsync,      /* fsync */
    ext_readdir,    /* readdir */
    ext_lookup,     /* lookup */
    ext_create,     /* create */
    ext_remove,     /* remove */
    ext_rename,     /* rename */
    ext_mkdir,      /* mkdir */
    ext_rmdir,      /* rmdir */
    ext_getattr,    /* getattr */
    ext_setattr,    /* setattr */
    ext_inactive,   /* inactive */
    ext_truncate,   /* truncate */
    ext_link,       /* link */
    ext_map_cached_page, /* cache (vop_cache) */
    ext_fallocate,  /* fallocate */
    ext_readlink,   /* read link */
    ext_symlink,    /* symbolic link */
};

