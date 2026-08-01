/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/file.h>
#include <osv/vfs_file.hh>
#include <osv/mmu.hh>
#include <atomic>

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

struct arc_hashkey {
    uint64_t key[4];
    bool operator==(const arc_hashkey& a) const noexcept {
        return memcmp(key, a.key, sizeof(key));
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
 * Unlike sync(), this function does not require a vfs_file* and does not throw.
 * It returns 0 on success or the first writeback errno.  Pages that fail to
 * write back are left marked dirty so the next pass retries them.  Intended
 * for use from filesystem VOP callbacks and from C code via
 * osv_pagecache_writeback_inode().
 */
int writeback_inode(dev_t dev, ino_t ino, off_t start, off_t end);

/*
 * writeback_all() — flush every dirty page in the entire write cache.
 * Used at clean shutdown and by the periodic writeback daemon.
 * Errors are silently swallowed.
 */
void writeback_all();

/*
 * pagecache_wb_interval_secs — seconds between periodic writeback passes.
 * Default: 5.  Writable at runtime; change takes effect at the next wakeup.
 * Atomic so a runtime writer does not race with the writeback daemon.
 */
extern std::atomic<unsigned> pagecache_wb_interval_secs;

void unmap_arc_buf(arc_buf_t* ab);
void map_arc_buf(hashkey* key, arc_buf_t* ab, void* page);
bool map_read_cached_page(hashkey *key, void *page);
}

#ifdef __cplusplus
extern "C" {
#endif

/*
 * osv_pagecache_writeback_inode() — C-linkage wrapper around
 * pagecache::writeback_inode().  Called from C filesystem code (e.g. ZFS
 * vop_fsync, ext2 vop_fsync) that has a vnode but not a vfs_file*.
 * Returns 0 on success or the first writeback errno.
 */
int osv_pagecache_writeback_inode(dev_t dev, ino_t ino, off_t start,
                                  off_t end);

/*
 * osv_pagecache_map_arc_page() — insert a borrowed ARC page into the read
 * cache without copying.  @page points into a pinned ZFS dbuf (db_data +
 * intra-record offset); @db_handle is the opaque dmu_buf_t* whose hold keeps
 * @page resident.  Ownership of the hold transfers to the page cache: the
 * cached page's destructor releases it via the callback registered through
 * osv_pagecache_register_arc_rele().
 *
 * If the key is already cached the hold is released immediately (via the
 * registered rele callback) and @page is not inserted, so a concurrent
 * prefetch/COW cannot leak a hold or double-map.
 *
 * Used by the OpenZFS 2.x integration (conf_zfs=openzfs); the legacy BSD-ZFS
 * ARC bridge (map_arc_buf/register_pagecache_arc_funs above) is a separate,
 * still-live path used only by conf_zfs=bsd.
 */
void osv_pagecache_map_arc_page(void *key, void *db_handle, void *page);

/*
 * osv_pagecache_register_arc_rele() — register the callback used to release a
 * borrowed ARC dbuf hold when its cached page is dropped.  Called once at ZFS
 * module init from libsolaris.so (OpenZFS path).
 */
void osv_pagecache_register_arc_rele(void (*rele)(void *db_handle));

#ifdef __cplusplus
}
#endif
