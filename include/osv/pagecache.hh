/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/file.h>
#include <osv/vfs_file.hh>
#include <osv/mmu.hh>

struct arc_buf;
typedef arc_buf arc_buf_t;

namespace pagecache {

struct hashkey {
    dev_t dev;
    ino_t ino;
    off_t offset;
    bool operator==(const hashkey& a) const noexcept {
        return (dev == a.dev) && (ino == a.ino) && (offset == a.offset);
    }
};


bool get(vfs_file* fp, off_t offset, mmu::hw_ptep<0> ptep, mmu::pt_element<0> pte, bool write, bool shared);
bool release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep<0> ptep);

/*
 * sync() — flush all dirty pages in [start, end) for the file described by fp.
 * Throws on I/O error.  Used by VOP_FSYNC implementations.
 */
void sync(vfs_file* fp, off_t start, off_t end);

/*
 * writeback_inode() — flush dirty write-cache pages for a specific (dev, ino)
 * inode in the byte range [start, end).
 *
 * Unlike sync(), this function does not require a vfs_file* and does not throw
 * on I/O error (errors are silently swallowed, consistent with background
 * writeback behaviour).  Intended for use from filesystem VOP callbacks and
 * from C code via osv_pagecache_writeback_inode().
 */
void writeback_inode(dev_t dev, ino_t ino, off_t start, off_t end);

/*
 * writeback_all() — flush every dirty page in the entire write cache.
 * Used at clean shutdown and by the periodic writeback daemon.
 * Errors are silently swallowed.
 */
void writeback_all();

/*
 * pagecache_wb_interval_secs — seconds between periodic writeback passes.
 * Default: 5.  Writable at runtime; change takes effect at the next wakeup.
 */
extern unsigned pagecache_wb_interval_secs;

void unmap_arc_buf(arc_buf_t* ab);
void map_arc_buf(hashkey* key, arc_buf_t* ab, void* page);
void map_read_cached_page(hashkey *key, void *page);
}

#ifdef __cplusplus
extern "C" {
#endif

/*
 * osv_pagecache_writeback_inode() — C-linkage wrapper around
 * pagecache::writeback_inode().  Called from C filesystem code (e.g. ZFS
 * vop_fsync, ext2 vop_fsync) that has a vnode but not a vfs_file*.
 */
void osv_pagecache_writeback_inode(dev_t dev, ino_t ino, off_t start,
                                   off_t end);

#ifdef __cplusplus
}
#endif
