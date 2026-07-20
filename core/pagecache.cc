/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <stack>
#include <vector>
#include <mutex>
#include <chrono>
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

// The OSv page cache serves two filesystem families through one set of
// entry points (get()/release()/sync()):
//
//   * ROFS and the OpenZFS 2.4.3 port route reads through read_cache.  Pages
//     are filled by the vop_cache() vnode hook.  The OpenZFS platform layer
//     deliberately does NOT tag the fsid with ZFS_ID (see
//     module/os/osv/zfs/zfs_vfsops.c), so IS_ZFS() returns false for its
//     files and they take the read_cache path exactly like ROFS.
//
//   * The old BSD-ZFS port tags its fsid with ZFS_ID, so IS_ZFS() returns
//     true and its reads take the ARC bridge below: mmap shares the ARC
//     buffer in place via arc_share_buf() instead of copying into a private
//     read_cache page.  The four function pointers are wired up at init by
//     register_pagecache_arc_funs(), called from fs/zfs/zfs_initialize.c in
//     libsolaris.so.  The access scanner feeds ARC's LRU by clearing the
//     accessed bit on shared pages.
//
// The per-file IS_ZFS(st.st_dev) check in get()/release()/sync() selects the
// path, so both stacks coexist.  When the old BSD-ZFS compat layer is retired
// the ARC bridge (the IS_ZFS() == true branches, cached_page_arc, the access
// scanner, and these function pointers) can be removed wholesale.

//These four function pointers will be set dynamically in INIT function of
//libsolaris.so by calling register_pagecache_arc_funs() below. The arc_unshare_buf(),
//arc_share_buf(), arc_buf_accessed() and arc_buf_get_hashkey()
//are functions defined in libsolaris.so.
void (*arc_unshare_buf_fun)(arc_buf_t*);
void (*arc_share_buf_fun)(arc_buf_t*);
void (*arc_buf_accessed_fun)(const uint64_t[4]);
void (*arc_buf_get_hashkey_fun)(arc_buf_t*, uint64_t[4]);

//This needs to be a C-style function so it can be called
//from libsolaris.so
extern "C" OSV_LIBSOLARIS_API void register_pagecache_arc_funs(
    void (*_arc_unshare_buf_fun)(arc_buf_t*),
    void (*_arc_share_buf_fun)(arc_buf_t*),
    void (*_arc_buf_accessed_fun)(const uint64_t[4]),
    void (*_arc_buf_get_hashkey_fun)(arc_buf_t*, uint64_t[4])) {
    arc_unshare_buf_fun = _arc_unshare_buf_fun;
    arc_share_buf_fun = _arc_share_buf_fun;
    arc_buf_accessed_fun = _arc_buf_accessed_fun;
    arc_buf_get_hashkey_fun = _arc_buf_get_hashkey_fun;
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
template<>
struct hash<pagecache::arc_hashkey> {
    size_t operator()(const pagecache::arc_hashkey& key) const noexcept {
        hash<uint64_t> h;
        return h(key.key[0]) ^ h(key.key[1]) ^ h(key.key[2]) ^ h(key.key[3]);
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
            // The page has no PTE mappings recorded.  Before readahead this was
            // impossible (every read_cache page had at least one mapping), but a
            // page loaded speculatively by readahead_if_sequential() sits in the
            // cache with no mapping until it is first faulted.  A COW write
            // fault on such a page reaches here with nothing to remove; report
            // zero remaining mappings so remove_read_mapping() drops the page
            // and the COW copy proceeds.
            return 0;
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

        vn_lock(_vp);
        error = VOP_WRITE(_vp, &uio, 0);
        vn_unlock(_vp);

        // Only clear the dirty flag once the data is safely on the
        // filesystem.  Clearing before VOP_WRITE would leave a failed
        // writeback looking clean, so sync()/fsync() retries and the
        // periodic flusher would silently drop the modified page.
        if (!error)
            _dirty = false;

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

class cached_page_arc;

static unsigned drop_arc_read_cached_page(cached_page_arc* cp, bool flush = true);

class cached_page_arc : public cached_page {
public:
    typedef std::unordered_multimap<arc_buf_t*, cached_page_arc*> arc_map;

    static arc_map arc_cache_map;

private:
    arc_buf_t* _ab;
    bool _removed = false;

    static arc_buf_t* ref(arc_buf_t* ab, cached_page_arc* pc)
    {
        arc_cache_map.emplace(ab, pc);
        return ab;
    }

    static bool unref(arc_buf_t* ab, cached_page_arc* pc)
    {
        auto it = arc_cache_map.equal_range(ab);

        arc_cache_map.erase(std::find(it.first, it.second, pc));

        return arc_cache_map.find(ab) == arc_cache_map.end();
    }

public:
    cached_page_arc(hashkey key, void* page, arc_buf_t* ab) : cached_page(key, page), _ab(ref(ab, this)) {}
    virtual ~cached_page_arc() {
        if (!_removed && unref(_ab, this)) {
            (*arc_unshare_buf_fun)(_ab);
        }
    }
    arc_buf_t* arcbuf() {
        return _ab;
    }
    static void unmap_arc_buf(arc_buf_t* ab) {
        auto it = arc_cache_map.equal_range(ab);
        unsigned count = 0;

        std::for_each(it.first, it.second, [&count](arc_map::value_type& p) {
                auto cp = p.second;
                cp->_removed = true;
                count += drop_arc_read_cached_page(cp, false);
        });
        arc_cache_map.erase(ab);
        if (count) {
            mmu::flush_tlb_all();
        }
    }
};

static bool operator==(const cached_page_arc::arc_map::value_type& l, const cached_page_arc* r) {
    return l.second == r;
}

std::unordered_multimap<arc_buf_t*, cached_page_arc*> cached_page_arc::arc_cache_map;
//Map used to store read cache pages for ZFS filesystem interacting with ARC
static std::unordered_map<hashkey, cached_page_arc*> arc_read_cache;
//Map used to store read cache pages for non-ZFS filesystems
static std::unordered_map<hashkey, cached_page*> read_cache;
static std::unordered_map<hashkey, cached_page_write*> write_cache;
static std::deque<cached_page_write*> write_lru;
static mutex arc_read_lock; // protects against parallel access to the ARC read cache
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

TRACEPOINT(trace_add_read_mapping, "buf=%p, addr=%p, ptep=%p", void*, void*, void*);
static void add_arc_read_mapping(cached_page_arc *cp, mmu::hw_ptep<0> ptep)
{
    trace_add_read_mapping(cp->arcbuf(), cp->addr(), ptep.release());
    add_read_mapping(cp, ptep);
}

template<typename T>
static void remove_read_mapping(std::unordered_map<hashkey, T>& cache, cached_page* cp, mmu::hw_ptep<0> ptep)
{
    if (cp->unmap(ptep) == 0) {
        cache.erase(cp->key());
        delete cp;
    }
}

TRACEPOINT(trace_remove_mapping, "buf=%p, addr=%p, ptep=%p", void*, void*, void*);
static void remove_arc_read_mapping(cached_page_arc* cp, mmu::hw_ptep<0> ptep)
{
    trace_remove_mapping(cp->arcbuf(), cp->addr(), ptep.release());
    remove_read_mapping(arc_read_cache, cp, ptep);
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

void remove_arc_read_mapping(hashkey& key, mmu::hw_ptep<0> ptep)
{
    SCOPE_LOCK(arc_read_lock);
    cached_page_arc* cp = find_in_cache(arc_read_cache, key);
    if (cp) {
        remove_arc_read_mapping(cp, ptep);
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

static unsigned drop_arc_read_cached_page(cached_page_arc* cp, bool flush)
{
    return drop_read_cached_page(arc_read_cache, cp, flush);
}

static void drop_read_cached_page(hashkey& key)
{
    SCOPE_LOCK(read_lock);
    cached_page* cp = find_in_cache(read_cache, key);
    if (cp) {
        drop_read_cached_page(read_cache, cp, true);
    }
}

TRACEPOINT(trace_drop_read_cached_page, "buf=%p, addr=%p", void*, void*);
static void drop_arc_read_cached_page(hashkey& key)
{
    SCOPE_LOCK(arc_read_lock);
    cached_page_arc* cp = find_in_cache(arc_read_cache, key);
    if (cp) {
        trace_drop_read_cached_page(cp->arcbuf(), cp->addr());
        drop_read_cached_page(arc_read_cache, cp, true);
    }
}

TRACEPOINT(trace_unmap_arc_buf, "buf=%p", void*);
void unmap_arc_buf(arc_buf_t* ab)
{
    trace_unmap_arc_buf(ab);
    SCOPE_LOCK(arc_read_lock);
    cached_page_arc::unmap_arc_buf(ab);
}

TRACEPOINT(trace_map_arc_buf, "buf=%p page=%p", void*, void*);
void map_arc_buf(hashkey *key, arc_buf_t* ab, void *page)
{
    trace_map_arc_buf(ab, page);
    SCOPE_LOCK(arc_read_lock);
    cached_page_arc* pc = new cached_page_arc(*key, page, ab);
    arc_read_cache.emplace(*key, pc);
    (*arc_share_buf_fun)(ab);
}

// Insert @page into the read cache under @key.  Returns true if the page was
// inserted (the cache now maps @key to it), or false if @key was already
// present (a concurrent VOP_CACHE won the race) and @page was NOT inserted.
//
// Page ownership stays with the caller: this function never frees @page.  The
// two callers have opposite contracts -- the ROFS vop_cache passes a borrowed
// page from its own read-around cache (must never be freed), while the ZFS
// vop_cache passes a freshly osv_alloc_page()'d page (must be freed on a
// collision).  So the caller decides what to do with a false return.
bool map_read_cached_page(hashkey *key, void *page)
{
    SCOPE_LOCK(read_lock);
    cached_page* pc = new cached_page(*key, page);
    auto res = read_cache.emplace(*key, pc);
    if (!res.second) {
        // Key already present; emplace() did not take ownership of the wrapper,
        // so free the wrapper (always OSv-owned).  Leave @page to the caller.
        delete pc;
        return false;
    }
    return true;
}

// C-linkage helpers used by ZFS vop_cache (zfs_vnops_os.c is a C file).
extern "C" void osv_pagecache_map_page(void *key, void *page)
{
    // The ZFS caller allocated @page with osv_alloc_page(); if it was not
    // inserted (a concurrent insert won), free it to avoid a leak.
    if (!map_read_cached_page(static_cast<hashkey*>(key), page)) {
        memory::free_page(page);
    }
}

extern "C" void *osv_alloc_page(void)
{
    return memory::alloc_page();
}

extern "C" void osv_free_page(void *p)
{
    memory::free_page(p);
}

// ------------------------------------------------------------------------
// OpenZFS 2.x borrowed-ARC-page path (conf_zfs=openzfs).
//
// OpenZFS 2.x made arc_share_buf() static, so the legacy BSD-ZFS ARC bridge
// (map_arc_buf/register_pagecache_arc_funs + cached_page_arc above, which
// tracks arc_buf_t* in arc_read_cache) cannot hook OpenZFS ARC buffers.
// Instead, the OpenZFS libsolaris.so inserts decompressed dbuf pages directly
// into the plain read_cache via osv_pagecache_map_arc_page(): the page is
// borrowed from a pinned dbuf (not owned by the cache) and the dbuf hold is
// released via a registered callback when the cached page is dropped.  This
// coexists with the BSD path -- the two use different caches
// (read_cache vs arc_read_cache) and different libsolaris.so builds.
static void (*arc_dbuf_rele)(void*) = nullptr;

// A read-cache page borrowed from a pinned ZFS ARC dbuf.  Unlike cached_page
// (whose _page it does not own) and cached_page_write (which frees its own
// page), this page IS owned by the ARC: we must not free it, but we must
// release the dbuf hold that keeps it resident when the page is dropped.
class cached_page_arc_borrow : public cached_page {
private:
    void* _db;
public:
    cached_page_arc_borrow(hashkey key, void* db, void* page)
        : cached_page(key, page), _db(db) {}
    virtual ~cached_page_arc_borrow() {
        if (_db && arc_dbuf_rele) {
            arc_dbuf_rele(_db);
        }
    }
};

// Insert a borrowed ARC page (see osv/pagecache.hh).  Ownership of the dbuf
// hold @db transfers to the read cache on success; if the key is already
// present the hold is released immediately so no double-map/leak occurs.
extern "C" void osv_pagecache_map_arc_page(void *key, void *db, void *page)
{
    hashkey* hk = static_cast<hashkey*>(key);
    SCOPE_LOCK(read_lock);
    if (find_in_cache(read_cache, *hk)) {
        if (arc_dbuf_rele) {
            arc_dbuf_rele(db);
        }
        return;
    }
    cached_page* cp = new cached_page_arc_borrow(*hk, db, page);
    read_cache.emplace(*hk, cp);
}

extern "C" void osv_pagecache_register_arc_rele(void (*rele)(void*))
{
    arc_dbuf_rele = rele;
}

/*
 * osv_free_pages() — return number of free physical pages.
 *
 * Called from arc_os.c (OpenZFS) so the ARC sees real memory pressure rather
 * than the static freemem value initialised at ZFS module load time.
 */
extern "C" unsigned long osv_free_pages(void)
{
    return memory::stats::free() / mmu::page_size;
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

#define IS_ZFS(st_dev) ((st_dev & (0xffULL<<56)) == ZFS_ID)

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
                if (IS_ZFS(st.st_dev)) {
                    drop_arc_read_cached_page(key);
                } else {
                    // ROFS (at least for now)
                    drop_read_cached_page(key);
                }
            } else {
                // remove mapping to read cache page if exists
                if (IS_ZFS(st.st_dev)) {
                    remove_arc_read_mapping(key, ptep);
                } else {
                    // ROFS (at least for now)
                    remove_read_mapping(key, ptep);
                }
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
        // read fault and page is not in write cache yet, return one from cache, mark it cow
        do {
            if (IS_ZFS(st.st_dev)) {
                WITH_LOCK(arc_read_lock) {
                    cached_page_arc* cp = find_in_cache(arc_read_cache, key);
                    if (cp) {
                        add_arc_read_mapping(cp, ptep);
                        return mmu::write_pte(cp->addr(), ptep, mmu::pte_mark_cow(pte, true));
                    }
                }
            } else {
                // ROFS (at least for now)
                WITH_LOCK(read_lock) {
                    cached_page* cp = find_in_cache(read_cache, key);
                    if (cp) {
                        add_read_mapping(cp, ptep);
                        return mmu::write_pte(cp->addr(), ptep, mmu::pte_mark_cow(pte, true));
                    }
                }
            }

            DROP_LOCK(write_lock) {
                // page is not in cache yet, create and try again
                // function may sleep so drop write lock while executing it
                ret = create_read_cached_page(fp, key);
                // Sequential readahead only applies to the read_cache path;
                // ZFS pages are shared in place from the ARC.
                if (ret == 0 && !IS_ZFS(st.st_dev))
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

    if (IS_ZFS(st.st_dev)) {
        WITH_LOCK(arc_read_lock) {
            cached_page_arc* rcp = find_in_cache(arc_read_cache, key);
            if (rcp && mmu::virt_to_phys(rcp->addr()) == old.addr()) {
                // page is in ARC read cache
                remove_arc_read_mapping(rcp, ptep);
                return false;
            }
        }
    } else {
        // ROFS (at least for now)
        WITH_LOCK(read_lock) {
            cached_page* rcp = find_in_cache(read_cache, key);
            if (rcp && mmu::virt_to_phys(rcp->addr()) == old.addr()) {
                // page is in regular read cache
                remove_read_mapping(read_cache, rcp, ptep);
                return false;
            }
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
     * step fsync() would miss those writes.  clear_dirty() only writes the
     * PTE when the dirty bit was set (leaving the mapping intact), so if no
     * page here was dirty no PTE is modified and the flush_tlb_all() below
     * can be safely skipped.
     */
    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->clear_dirty())
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
            // Re-mark dirty: phase 2 cleared the flag, but the data never
            // reached the filesystem, so it must not be treated as clean.
            cp->mark_dirty();
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
     * concurrent TLB invalidation.  clear_dirty() writes the PTE only when
     * the dirty bit was set (mapping preserved), so an empty to_flush means
     * no PTE changed and the flush_tlb_all() below can be skipped. */
    for (auto& kv : write_cache) {
        cached_page_write* cp = kv.second;
        if (cp->clear_dirty())
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
     * is still held, consistent with the existing pagecache::sync() path).
     * On error, re-mark dirty so the page is retried on the next pass rather
     * than silently dropped. */
    for (auto cp : to_flush) {
        if (cp->writeback())
            cp->mark_dirty();
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
int writeback_inode(dev_t dev, ino_t ino, off_t start, off_t end)
{
    hashkey key {dev, ino, 0};
    std::vector<cached_page_write*> to_flush;

    SCOPE_LOCK(write_lock);

    /* Phase 1: promote PTE-dirty pages to software-dirty.  clear_dirty()
     * writes the PTE only when the dirty bit was set (mapping preserved),
     * so an empty to_flush means no PTE changed and the flush below can be
     * skipped. */
    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->clear_dirty())
            cp->mark_dirty();
    }

    /* Phase 2: collect and clear the software-dirty flag. */
    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->clear_dirty_flag())
            to_flush.push_back(cp);
    }

    if (to_flush.empty())
        return 0;

    mmu::flush_tlb_all();

    /* Phase 3: write back.  On error, re-mark dirty so the page is retried
     * and report the first error to the caller (fsync durability). */
    int first_error = 0;
    for (auto cp : to_flush) {
        int err = cp->writeback();
        if (err) {
            cp->mark_dirty();
            if (!first_error)
                first_error = err;
        }
    }
    return first_error;
}

/*
 * writeback_all() — flush the entire write cache.
 * Thin wrapper around flush_write_cache_dirty() for external callers.
 */
void writeback_all()
{
    flush_write_cache_dirty();
}

std::atomic<unsigned> pagecache_wb_interval_secs{5};

static sched::thread* pagecache_wb_thread;

static void writeback_worker()
{
    while (true) {
        /* Sleep pagecache_wb_interval_secs between writeback passes.
         * The interval is re-read on each iteration so a runtime change
         * takes effect at the next wakeup without restarting the thread.
         * Atomic load avoids a data race with a concurrent writer. */
        unsigned interval = pagecache_wb_interval_secs.load(std::memory_order_relaxed);
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

TRACEPOINT(trace_access_scanner, "scanned=%u, cleared=%u, %%cpu=%g", unsigned, unsigned, double);
class access_scanner {
    static constexpr double _max_cpu = 20;
    static constexpr double _min_cpu = 0.1;
    static constexpr unsigned _freq = 1000;
    double _cpu = _min_cpu;
    std::unique_ptr<sched::thread> _thread;
public:
    access_scanner() : _thread(sched::thread::make(std::bind(&access_scanner::run, this), sched::thread::attr().name("page-access-scanner"))) {
        _thread->start();
    }

private:
    bool mark_accessed(std::unordered_set<arc_hashkey>& accessed) {
        if (accessed.empty()) {
            return false;
        }
        for (auto&& arc_hashkey: accessed) {
            (*arc_buf_accessed_fun)(arc_hashkey.key);
        }
        accessed.clear();
        return true;
    }
    void run()
    {
        cached_page_arc::arc_map::size_type current_bucket = 0;
        std::unordered_set<arc_hashkey> accessed;
        unsigned scanned = 0, cleared = 0;

        while (true) {
            unsigned buckets_scanned = 0;
            bool flush = false;
            auto bucket_count = cached_page_arc::arc_cache_map.bucket_count();

            double work = (1000000000 * _cpu)/100;
            double sleep = 1000000000 - work;

            sched::thread::sleep(std::chrono::nanoseconds(static_cast<unsigned long>(sleep/_freq)));

            auto start = sched::thread::current()->thread_clock();
            auto deadline = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::nanoseconds(static_cast<unsigned long>(work/_freq))) + start;

            WITH_LOCK(arc_read_lock) {
                while (sched::thread::current()->thread_clock() < deadline && buckets_scanned < bucket_count) {
                    if (current_bucket >= cached_page_arc::arc_cache_map.bucket_count()) {
                        current_bucket = 0;
                    }
                    std::for_each(cached_page_arc::arc_cache_map.begin(current_bucket), cached_page_arc::arc_cache_map.end(current_bucket),
                            [&accessed, &scanned, &cleared](cached_page_arc::arc_map::value_type& p) {
                        auto arcbuf = p.first;
                        auto cp = p.second;
                        if (cp->clear_accessed()) {
                            arc_hashkey arc_hashkey;
                            (*arc_buf_get_hashkey_fun)(arcbuf, arc_hashkey.key);
                            accessed.emplace(arc_hashkey);
                            cleared++;
                        }
                        scanned++;
                    });
                    current_bucket++;
                    buckets_scanned++;

                    // mark ARC buffers as accessed when we have 1024 of them
                    if (!(cleared % 1024)) {
                        DROP_LOCK(arc_read_lock) {
                            flush = mark_accessed(accessed);
                        }
                    }
                }
            }

            // mark leftovers ARC buffers as accessed
            flush = mark_accessed(accessed);

            if (flush) {
                mmu::flush_tlb_all();
            }

            if (buckets_scanned == bucket_count || !scanned) {
                _cpu = _min_cpu;
            } else {
                _cpu = std::max(_min_cpu, std::min(_max_cpu, _cpu * ((cleared*5.0)/scanned)));
            }

            trace_access_scanner(scanned, cleared, _cpu);

            // decay old results a bit
            scanned /= 4;
            cleared /= 2;
        }
    }
};

static access_scanner *s_access_scanner = nullptr;

constexpr double access_scanner::_max_cpu;
constexpr double access_scanner::_min_cpu;

}

//The access_scanner thread is ZFS specific so it
//is initialized by calling the function below if libsolaris.so
//is loaded.  Only the old BSD-ZFS port calls this (via zfs_initialize.c);
//the OpenZFS port routes reads through read_cache and never uses the scanner.
extern "C" OSV_LIBSOLARIS_API void start_pagecache_access_scanner() {
    pagecache::s_access_scanner = new pagecache::access_scanner();
}

/*
 * osv_pagecache_writeback_inode() — C-linkage entry point.
 *
 * Allows C filesystem code (ZFS vop_fsync, ext2 vop_fsync, etc.) to request
 * writeback of dirty mmap pages for a specific inode without needing a C++
 * vfs_file* handle.  Returns 0 on success or the first writeback errno so
 * fsync() can honor durability.
 */
extern "C" int osv_pagecache_writeback_inode(dev_t dev, ino_t ino,
                                              off_t start, off_t end)
{
    return pagecache::writeback_inode(dev, ino, start, end);
}
