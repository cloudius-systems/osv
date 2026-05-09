/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <vector>
#include <mutex>
#include <algorithm>
#include <boost/variant.hpp>
#include <osv/pagecache.hh>
#include <osv/mempool.hh>
#include <osv/export.h>
#include <fs/vfs/vfs.h>
#include <fs/vfs/vfs_id.h>
#include <osv/trace.hh>
#include <osv/prio.hh>
#include <osv/sched.hh>
#include <osv/clock.hh>

// NOTE: The ARC bridge (IS_ZFS() == true path) is intentionally unreachable
// with the OpenZFS 2.x integration. OpenZFS 2.x made arc_share_buf() a static
// function, breaking the hook that the original BSD-ZFS used to share ARC pages
// directly with the mmap layer. ZFS files now use the regular read_cache path,
// fed by the zfs_vop_cache() vnode hook which calls zfs_read() per page.
//
// No double-caching occurs: ARC caches compressed ZFS blocks; read_cache caches
// decompressed 4KB pages for mmap — they serve different granularities.
//
// The functions register_pagecache_arc_funs(), start_pagecache_access_scanner(),
// unmap_arc_buf(), and map_arc_buf() are kept as no-ops below because
// fs/zfs/zfs_initialize.c (linked into libsolaris.so) still references them.
// A future cleanup can remove both sides once the old BSD-ZFS compat layer is
// fully retired.

// No-op: kept for ABI compatibility with fs/zfs/zfs_initialize.c in libsolaris.so.
extern "C" OSV_LIBSOLARIS_API void register_pagecache_arc_funs(
    void (*)(arc_buf_t*),
    void (*)(arc_buf_t*),
    void (*)(const uint64_t[4]),
    void (*)(arc_buf_t*, uint64_t[4])) {
    // ARC bridge inactive with OpenZFS 2.x — see comment above.
}

namespace std {
template<>
struct hash<pagecache::hashkey> {
    size_t operator()(const pagecache::hashkey key) const noexcept {
        hash<uint64_t> h;
        return h(key.dev) ^ h(key.ino) ^ h(key.offset);
    }
};

template<> struct hash<mmu::hw_ptep<0>> {
    size_t operator()(const mmu::hw_ptep<0>& ptep) const noexcept {
        hash<const mmu::pt_element<0>*> h;
        return h(ptep.release());
    }
};

std::unordered_set<mmu::hw_ptep<0>>::iterator begin(std::unique_ptr<std::unordered_set<mmu::hw_ptep<0>>> const& e)
{
    return e->begin();
}

std::unordered_set<mmu::hw_ptep<0>>::iterator end(std::unique_ptr<std::unordered_set<mmu::hw_ptep<0>>> const& e)
{
    return e->end();
}

}

namespace pagecache {

static unsigned lru_max_length = 100;
static unsigned lru_free_count = 20;
constexpr unsigned max_lru_free_count = 200;
static void* zero_page;

void  __attribute__((constructor(init_prio::pagecache))) setup()
{
    lru_max_length = std::max(memory::phys_mem_size / memory::page_size / 100, size_t(100));
    lru_free_count = std::min(lru_max_length/5, max_lru_free_count);
    zero_page = memory::alloc_page();
    memset(zero_page, 0, mmu::page_size);
}

class cached_page {
protected:
    const hashkey _key;
    void* _page;
    typedef boost::variant<std::nullptr_t, mmu::hw_ptep<0>, std::unique_ptr<std::unordered_set<mmu::hw_ptep<0>>>> ptep_list;
    ptep_list _ptes; // set of pointers to ptes that map the page

    template<typename T>
    class ptes_visitor : public boost::static_visitor<T> {
    protected:
        ptep_list& _ptes;
    public:
        ptes_visitor(ptep_list& ptes) : _ptes(ptes) {}
    };
    class ptep_add : public ptes_visitor<void> {
        mmu::hw_ptep<0>& _ptep;
    public:
        ptep_add(ptep_list& ptes, mmu::hw_ptep<0>& ptep) : ptes_visitor(ptes), _ptep(ptep) {}
        void operator()(std::nullptr_t& v) {
            _ptes = _ptep;
        }
        void operator()(mmu::hw_ptep<0>& ptep) {
            auto ptes = std::unique_ptr<std::unordered_set<mmu::hw_ptep<0>>>(new std::unordered_set<mmu::hw_ptep<0>>({ptep}));
            ptes->emplace(_ptep);
            _ptes = std::move(ptes);
        }
        void operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep<0>>>& set) {
            set->emplace(_ptep);
        }
    };
    class ptep_remove : public ptes_visitor<int> {
        mmu::hw_ptep<0>& _ptep;
    public:
        ptep_remove(ptep_list& ptes, mmu::hw_ptep<0>& ptep) : ptes_visitor(ptes), _ptep(ptep) {}
        int operator()(std::nullptr_t &v) {
            assert(0);
            return -1;
        }
        int operator()(mmu::hw_ptep<0>& ptep) {
            _ptes = nullptr;
            return 0;
        }
        int operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep<0>>>& set) {
            set->erase(_ptep);
            if (set->size() == 1) {
                auto pte = *(set->begin());
                _ptes = pte;
                return 1;
            }
            return set->size();
        }
    };

    template<typename Map, typename Reduce, typename Ret>
    class map_reduce : public boost::static_visitor<Ret> {
    private:
        Map _mapper;
        Reduce _reducer;
        Ret _initial;
    public:
        map_reduce(Map mapper, Reduce reducer, Ret initial) : _mapper(mapper), _reducer(reducer), _initial(initial) {}
        Ret operator()(std::nullptr_t &v) {
            return _initial;
        }
        Ret operator()(mmu::hw_ptep<0>& ptep) {
            return _reducer(_initial, _mapper(ptep));
        }
        Ret operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep<0>>>& set) {
            Ret acc = _initial;
            for (auto&& i: set) {
              acc = _reducer(acc, _mapper(i));
            }
            return acc;
        }
    };

    template <typename Map, typename Reduce = std::plus<int>, typename Ret = int>
    Ret for_each_pte(Map mapper, Reduce reducer = std::plus<int>(), Ret initial = 0)
    {
        map_reduce<Map, Reduce, Ret> mr(mapper, reducer, initial);
        return boost::apply_visitor(mr, _ptes);
    }

public:
    cached_page(hashkey key, void* page) : _key(key), _page(page) {
    }
    virtual ~cached_page() {
    }

    void map(mmu::hw_ptep<0> ptep) {
        ptep_add add(_ptes, ptep);
        boost::apply_visitor(add, _ptes);
    }
    int unmap(mmu::hw_ptep<0> ptep) {
        ptep_remove rm(_ptes, ptep);
        return boost::apply_visitor(rm, _ptes);
    }
    void* addr() {
        return _page;
    }
    int flush() {
        return for_each_pte([] (mmu::hw_ptep<0> pte) { mmu::clear_pte(pte); return 1;});
    }
    int clear_accessed() {
        return for_each_pte([] (mmu::hw_ptep<0> pte) -> int { return mmu::clear_accessed(pte); });
    }
    int clear_dirty() {
        return for_each_pte([] (mmu::hw_ptep<0> pte) -> int { return mmu::clear_dirty(pte); });
    }
    const hashkey& key() {
        return _key;
    }
};

class cached_page_write : public cached_page {
private:
    struct vnode* _vp;
    bool _dirty = false;
public:
    cached_page_write(hashkey key, vfs_file* fp) : cached_page(key, memory::alloc_page()) {
        _vp = fp->f_dentry->d_vnode;
        vref(_vp);
    }
    virtual ~cached_page_write() {
        if (_page) {
            if (_dirty) {
                writeback();
            }
            memory::free_page(_page);
            vrele(_vp);
        }
    }
    int writeback()
    {
        int error;
        struct iovec iov {_page, mmu::page_size};
        struct uio uio {&iov, 1, _key.offset, mmu::page_size, UIO_WRITE};

        _dirty = false;

        vn_lock(_vp);
        error = VOP_WRITE(_vp, &uio, 0);
        vn_unlock(_vp);

        return error;
    }
    void* release() { // called to demote a page from cache page to anonymous
        assert(boost::get<std::nullptr_t>(_ptes) == nullptr);
        void *p = _page;
        _page = nullptr;
        vrele(_vp);
        return p;
    }
    void mark_dirty() {
        _dirty |= true;
    }
    bool is_dirty() const { return _dirty; }
    bool clear_dirty_flag() {
        bool was = _dirty;
        _dirty = false;
        return was;
    }
    bool flush_check_dirty() {
        return for_each_pte([] (mmu::hw_ptep<0> pte) { return mmu::clear_pte(pte).dirty(); }, std::logical_or<bool>(), false);
    }
};

static std::unordered_map<hashkey, cached_page*> read_cache;
static std::unordered_map<hashkey, cached_page_write*> write_cache;
static std::deque<cached_page_write*> write_lru;
static mutex read_lock; // protects against parallel access to the read cache
static mutex write_lock; // protect against parallel access to the write cache

template<typename T>
static T find_in_cache(std::unordered_map<hashkey, T>& cache, hashkey& key)
{
    auto cpi = cache.find(key);

    if (cpi == cache.end()) {
        return nullptr;
    } else {
        return cpi->second;
    }
}

static void add_read_mapping(cached_page *cp, mmu::hw_ptep<0> ptep)
{
    cp->map(ptep);
}

template<typename T>
static void remove_read_mapping(std::unordered_map<hashkey, T>& cache, cached_page* cp, mmu::hw_ptep<0> ptep)
{
    if (cp->unmap(ptep) == 0) {
        cache.erase(cp->key());
        delete cp;
    }
}

void remove_read_mapping(hashkey& key, mmu::hw_ptep<0> ptep)
{
    SCOPE_LOCK(read_lock);
    cached_page* cp = find_in_cache(read_cache, key);
    if (cp) {
        remove_read_mapping(read_cache, cp, ptep);
        // The method remove_read_mapping() is called by pagecache::get()
        // to handle MAP_PRIVATE COW (Copy-On-Write) scenario triggered by an attempt to write
        // to read-only page in read_cache (write protection page-fault). To handle it properly
        // we need to remove the existing mapping for specified file hash (ino, offset) so that
        // the physical address of newly allocated read-write page with the exact copy of
        // the original data can be placed in pte instead.
        // Normally this works fine and the application continues after and reads from/writes to
        // new allocated page. But sometimes when applications "mmap" same portions of the same file
        // with MAP_PRIVATE flag at the same time from multiple threads under load,
        // those threads get migrated to different CPUs so it is important we flush tlb
        // on all cpus. Otherwise given thread after migrated to different CPU may still see old
        // read-only page with stale data and cause spectacular crash.
        // The fact we have to flush tlb is very unfortunate as it is pretty expensive operation.
        // On positive side most applications loaded from Read-Only FS will experience pretty limited
        // number of COW faults mostly caused by ELF linker writing to GOT or PLT section.
        // TODO 1: Investigate if there is an alternative "cheaper" way to solve this problem.
        // TODO 2: Investigate why flushing TLB is necessary in single CPU scenario.
        // TODO 3: Investigate why we do not have to flush tlb when removing read mapping with ZFS.
        mmu::flush_tlb_all();
    }
}

template<typename T>
static unsigned drop_read_cached_page(std::unordered_map<hashkey, T>& cache, cached_page* cp, bool flush)
{
    int flushed = cp->flush();
    cache.erase(cp->key());

    if (flush && flushed > 1) { // if there was only one pte it is the one we are faulting on; no need to flush.
        mmu::flush_tlb_all();
    }

    delete cp;

    return flushed;
}

static void drop_read_cached_page(hashkey& key)
{
    SCOPE_LOCK(read_lock);
    cached_page* cp = find_in_cache(read_cache, key);
    if (cp) {
        drop_read_cached_page(read_cache, cp, true);
    }
}

// No-op: ARC bridge inactive with OpenZFS 2.x (see comment near register_pagecache_arc_funs).
// Kept for ABI compatibility with bsd/porting/mmu.cc in loader.elf.
void unmap_arc_buf(arc_buf_t*) {}

// No-op: ARC bridge inactive with OpenZFS 2.x (see comment near register_pagecache_arc_funs).
// Kept for ABI compatibility with bsd/porting/mmu.cc in loader.elf.
void map_arc_buf(hashkey*, arc_buf_t*, void*) {}

void map_read_cached_page(hashkey *key, void *page)
{
    SCOPE_LOCK(read_lock);
    cached_page* pc = new cached_page(*key, page);
    read_cache.emplace(*key, pc);
}

// C-linkage helpers used by ZFS vop_cache (zfs_vnops_os.c is a C file).
extern "C" void osv_pagecache_map_page(void *key, void *page)
{
    map_read_cached_page(static_cast<hashkey*>(key), page);
}

extern "C" void *osv_alloc_page(void)
{
    return memory::alloc_page();
}

extern "C" void osv_free_page(void *p)
{
    memory::free_page(p);
}

/*
 * osv_pagecache_read_page() — copy a cached read page into @buf.
 *
 * If (dev, ino, offset) is in the read cache, copies PAGE_SIZE bytes into
 * @buf and returns 0 (cache hit).  Returns -1 on miss.
 *
 * Used by vop_read implementations to serve sequential reads from the
 * page cache without triggering a mmap fault.
 */
extern "C" int osv_pagecache_read_page(dev_t dev, ino_t ino, off_t offset,
                                        void *buf)
{
    hashkey key {dev, ino, offset};
    SCOPE_LOCK(read_lock);
    cached_page *cp = find_in_cache(read_cache, key);
    if (!cp)
        return -1;
    memcpy(buf, cp->addr(), mmu::page_size);
    return 0;
}

/*
 * osv_pagecache_map_page_if_absent() — insert page only when key is absent.
 *
 * If (dev, ino, offset) is already in the read cache the new @page is NOT
 * inserted and the caller remains responsible for freeing it.  Returns 1
 * if insertion was skipped (page already cached), 0 if inserted (read_cache
 * now owns the page).
 *
 * Used by vop_read implementations to warm the page cache after reading
 * file data from disk, so a subsequent mmap fault finds the page cached.
 */
extern "C" int osv_pagecache_map_page_if_absent(dev_t dev, ino_t ino,
                                                  off_t offset, void *page)
{
    hashkey key {dev, ino, offset};
    SCOPE_LOCK(read_lock);
    if (find_in_cache(read_cache, key))
        return 1;   /* already cached — caller owns page */
    cached_page *cp = new cached_page(key, page);
    read_cache.emplace(key, cp);
    return 0;       /* inserted — read_cache owns page */
}

static int create_read_cached_page(vfs_file* fp, hashkey& key)
{
    return fp->read_page_from_cache(&key, key.offset);
}

/*
 * Sequential readahead
 * --------------------
 * READAHEAD_WINDOW: number of pages to prefetch ahead when sequential
 * access is detected (previous page in read_cache).  Four pages == 16 KB
 * of lookahead, enough to hide one round-trip of I/O latency while the
 * CPU processes the current page.
 */
static constexpr int READAHEAD_WINDOW = 4;

/*
 * prefetch_one_page() — speculatively load one page into the read cache.
 *
 * Called for readahead: errors are silently ignored (prefetch is best-effort).
 * write_lock must NOT be held by the caller; this function acquires and
 * releases it internally via VOP_CACHE → osv_pagecache_map_page.
 */
static void prefetch_one_page(vfs_file* fp, hashkey key, off_t file_size)
{
    struct vnode *vp = fp->f_dentry->d_vnode;

    if (!vp->v_op->vop_cache)
        return;
    if (key.offset >= file_size)
        return;

    WITH_LOCK(read_lock) {
        if (find_in_cache(read_cache, key))
            return;  /* already cached */
    }

    iovec io[1];
    io[0].iov_base = &key;   /* hashkey* used as opaque slot key */
    uio data;
    data.uio_iov     = io;
    data.uio_iovcnt  = 1;
    data.uio_offset  = key.offset;
    data.uio_resid   = mmu::page_size;
    data.uio_rw      = UIO_READ;

    vn_lock(vp);
    VOP_CACHE(vp, fp, &data);  /* errors silently ignored for prefetch */
    vn_unlock(vp);
}

/*
 * readahead_if_sequential() — heuristic sequential prefetch.
 *
 * If the page just before @key is in read_cache (sequential-access signal),
 * speculatively populate the next READAHEAD_WINDOW pages.
 *
 * write_lock is released by the caller (DROP_LOCK block) so we can safely
 * acquire vnode locks inside prefetch_one_page().
 */
static void readahead_if_sequential(vfs_file* fp, const hashkey& key)
{
    if (key.offset < (off_t)mmu::page_size)
        return;

    hashkey prev {key.dev, key.ino, key.offset - (off_t)mmu::page_size};
    {
        WITH_LOCK(read_lock) {
            if (!find_in_cache(read_cache, prev))
                return;  /* not sequential — skip readahead */
        }
    }

    /* Sequential: prefetch READAHEAD_WINDOW pages starting from key+1 */
    off_t file_size = fp->f_dentry->d_vnode->v_size;
    for (int i = 1; i <= READAHEAD_WINDOW; i++) {
        hashkey ahead {key.dev, key.ino,
                       key.offset + (off_t)(i * (int)mmu::page_size)};
        prefetch_one_page(fp, ahead, file_size);
    }
}

static std::unique_ptr<cached_page_write> create_write_cached_page(vfs_file* fp, hashkey& key)
{
    size_t bytes;
    cached_page_write* cp = new cached_page_write(key, fp);
    struct iovec iov {cp->addr(), mmu::page_size};

    assert(sys_read(fp, &iov, 1, key.offset, &bytes) == 0);
    return std::unique_ptr<cached_page_write>(cp);
}

TRACEPOINT(trace_drop_write_cached_page, "addr=%p", void*);
static void insert(cached_page_write* cp) {
    static cached_page_write* tofree[max_lru_free_count];
    write_cache.emplace(cp->key(), cp);
    write_lru.push_front(cp);

    if (write_lru.size() > lru_max_length) {
        for (unsigned i = 0; i < lru_free_count; i++) {
            cached_page_write *p = write_lru.back();
            write_lru.pop_back();
            trace_drop_write_cached_page(p->addr());
            write_cache.erase(p->key());
            if (p->flush_check_dirty()) {
                p->mark_dirty();
            }
            tofree[i] = p;
        }
        mmu::flush_tlb_all();
        for (auto p: tofree) {
            delete p;
        }
    }
}

bool get(vfs_file* fp, off_t offset, mmu::hw_ptep<0> ptep, mmu::pt_element<0> pte, bool write, bool shared)
{
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, offset};
    SCOPE_LOCK(write_lock);
    cached_page_write* wcp = find_in_cache(write_cache, key);

    if (write) {
        if (!wcp) {
            auto newcp = create_write_cached_page(fp, key);
            if (shared) {
                // write fault into shared mapping, there page is not in write cache yet, add it.
                wcp = newcp.release();
                insert(wcp);
                // page is moved from read cache to write cache
                // drop read page if exists, removing all mappings
                drop_read_cached_page(key);
            } else {
                // remove mapping to read cache page if exists
                remove_read_mapping(key, ptep);
                // cow (copy-on-write) of private page from read cache
                return mmu::write_pte(newcp->release(), ptep, pte);
            }
        } else if (!shared) {
            // cow (copy-on-write) of private page from write cache
            void* page = memory::alloc_page();
            memcpy(page, wcp->addr(), mmu::page_size);
            return mmu::write_pte(page, ptep, pte);
        }
    } else if (!wcp) {
        int ret;
        // read fault and page is not in write cache yet, return one from read cache, mark it cow
        do {
            WITH_LOCK(read_lock) {
                cached_page* cp = find_in_cache(read_cache, key);
                if (cp) {
                    add_read_mapping(cp, ptep);
                    return mmu::write_pte(cp->addr(), ptep, mmu::pte_mark_cow(pte, true));
                }
            }

            DROP_LOCK(write_lock) {
                // page is not in cache yet, create and try again
                // function may sleep so drop write lock while executing it
                ret = create_read_cached_page(fp, key);
                if (ret == 0)
                    readahead_if_sequential(fp, key);
            }

            // we dropped write lock, need to re-check write cache again
            wcp = find_in_cache(write_cache, key);
            if (wcp) {
                // write cache page appeared while we were creating a read cache page from ARC
                // return will cause faulting thread to re-fault and we will try again
                return false;
            }

        } while (ret != -1);

        // try to access a hole in a file, map by zero_page
        return mmu::write_pte(zero_page, ptep, mmu::pte_mark_cow(pte, true));
    }

    wcp->map(ptep);

    return mmu::write_pte(wcp->addr(), ptep, mmu::pte_mark_cow(pte, !shared));
}

bool release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep<0> ptep)
{
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, offset};

    auto old = clear_pte(ptep);

    // page is either in ARC cache or write cache or zero page or private page

    WITH_LOCK(write_lock) {
        cached_page_write* wcp = find_in_cache(write_cache, key);

        if (wcp && mmu::virt_to_phys(wcp->addr()) == old.addr()) {
            // page is in write cache
            wcp->unmap(ptep);
            if (old.dirty()) {
                // unmapped pte was dirty, mark page dirty for writeback
                wcp->mark_dirty();
            }
            return false;
        }
    }

    WITH_LOCK(read_lock) {
        cached_page* rcp = find_in_cache(read_cache, key);
        if (rcp && mmu::virt_to_phys(rcp->addr()) == old.addr()) {
            // page is in read cache
            remove_read_mapping(read_cache, rcp, ptep);
            return false;
        }
    }

    // if a private page, caller will free it
    return addr != zero_page;
}

void sync(vfs_file* fp, off_t start, off_t end)
{
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, 0};

    std::vector<cached_page_write*> to_flush;

    SCOPE_LOCK(write_lock);

    /* Phase 1: promote PTE-dirty pages to software-dirty.
     * A page written via mmap (MAP_SHARED) may have the dirty bit set only
     * in its PTE, with the software _dirty flag still false.  Without this
     * step fsync() would miss those writes.
     */
    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->flush_check_dirty())
            cp->mark_dirty();
    }

    /* Phase 2: collect pages with the software-dirty flag set and clear it. */
    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->clear_dirty_flag())
            to_flush.push_back(cp);
    }

    if (to_flush.empty())
        return;

    mmu::flush_tlb_all();

    /* Phase 3: write each page back to the filesystem. */
    for (auto cp : to_flush) {
        auto err = cp->writeback();
        if (err) {
            throw make_error(err);
        }
    }
}

/*
 * Periodic writeback
 * ------------------
 * flush_write_cache_dirty() flushes every dirty page in the write cache.
 * Pages become dirty in two ways:
 *   1. mark_dirty() called from release() when a writeable PTE is unmapped
 *      with the dirty bit set (MAP_SHARED writes).
 *   2. flush_check_dirty() + mark_dirty() called at LRU eviction time.
 *
 * Without a periodic flush, pages in (1) stay dirty indefinitely until
 * eviction or an explicit fsync/sync call.  The writeback thread below
 * ensures a 5-second upper bound on how long dirty data remains in DRAM.
 */
static void flush_write_cache_dirty()
{
    std::vector<cached_page_write*> to_flush;

    SCOPE_LOCK(write_lock);

    /* Phase 1: promote any PTE-dirty pages to software-dirty so they survive
     * the writeback phase even if the PTE dirty bit gets cleared by a
     * concurrent TLB invalidation. */
    for (auto& kv : write_cache) {
        cached_page_write* cp = kv.second;
        if (cp->flush_check_dirty())
            cp->mark_dirty();
    }

    /* Phase 2: collect and clear software-dirty flag. */
    for (auto& kv : write_cache) {
        cached_page_write* cp = kv.second;
        if (cp->clear_dirty_flag())
            to_flush.push_back(cp);
    }

    if (to_flush.empty())
        return;

    mmu::flush_tlb_all();

    /* Phase 3: write back (vnode lock acquired inside writeback(); write_lock
     * is still held, consistent with the existing pagecache::sync() path). */
    for (auto cp : to_flush) {
        cp->writeback();
    }
}

/*
 * writeback_inode() — flush dirty pages for a specific (dev, ino) range.
 *
 * Same three-phase approach as sync() but:
 *   - accepts (dev, ino) directly instead of a vfs_file*
 *   - does not throw on I/O error (background/advisory writeback)
 *
 * The write_lock is held across all three phases so that a concurrent
 * eviction cannot free a page between phase 2 (collecting) and phase 3
 * (writing back).  This matches the locking strategy in sync().
 */
void writeback_inode(dev_t dev, ino_t ino, off_t start, off_t end)
{
    hashkey key {dev, ino, 0};
    std::vector<cached_page_write*> to_flush;

    SCOPE_LOCK(write_lock);

    /* Phase 1: promote PTE-dirty pages to software-dirty. */
    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->flush_check_dirty())
            cp->mark_dirty();
    }

    /* Phase 2: collect and clear the software-dirty flag. */
    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->clear_dirty_flag())
            to_flush.push_back(cp);
    }

    if (to_flush.empty())
        return;

    mmu::flush_tlb_all();

    /* Phase 3: write back; errors are swallowed (advisory path). */
    for (auto cp : to_flush)
        cp->writeback();
}

/*
 * writeback_all() — flush the entire write cache.
 * Thin wrapper around flush_write_cache_dirty() for external callers.
 */
void writeback_all()
{
    flush_write_cache_dirty();
}

unsigned pagecache_wb_interval_secs = 5;

static sched::thread* pagecache_wb_thread;

static void writeback_worker()
{
    while (true) {
        /* Sleep pagecache_wb_interval_secs between writeback passes.
         * The interval is re-read on each iteration so a runtime change
         * takes effect at the next wakeup without restarting the thread. */
        unsigned interval = pagecache_wb_interval_secs;
        if (interval == 0)
            interval = 5;  /* guard against zero to avoid busy-loop */
        sched::timer t(*sched::thread::current());
        t.set(std::chrono::seconds(interval));
        sched::thread::wait_until([&] { return t.expired(); });

        flush_write_cache_dirty();
    }
}

/*
 * pagecache_start_writeback() — start the periodic writeback daemon.
 *
 * Called once from the pagecache constructor after the scheduler is up.
 * A plain constructor (no init_prio) runs after all init_prio constructors,
 * at which point the OSv scheduler is already running.
 */
static void __attribute__((constructor)) pagecache_start_writeback()
{
    pagecache_wb_thread = sched::thread::make(
        [] { writeback_worker(); },
        sched::thread::attr().name("pagecache-wb"));
    pagecache_wb_thread->start();
}

}

// No-op: ARC access scanner is not used with OpenZFS 2.x (see comment near
// register_pagecache_arc_funs). Kept for ABI compatibility with libsolaris.so.
extern "C" OSV_LIBSOLARIS_API void start_pagecache_access_scanner() {}

/*
 * osv_pagecache_writeback_inode() — C-linkage entry point.
 *
 * Allows C filesystem code (ZFS vop_fsync, ext2 vop_fsync, etc.) to request
 * writeback of dirty mmap pages for a specific inode without needing a C++
 * vfs_file* handle.  Errors are silently swallowed (same as background WB).
 */
extern "C" void osv_pagecache_writeback_inode(dev_t dev, ino_t ino,
                                               off_t start, off_t end)
{
    pagecache::writeback_inode(dev, ino, start, end);
}
