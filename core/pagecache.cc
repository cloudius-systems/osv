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
#include <boost/variant.hpp>
#include <osv/pagecache.hh>
#include <osv/mempool.hh>
#include <osv/export.h>
#include <fs/vfs/vfs.h>
#include <fs/vfs/vfs_id.h>
#include <osv/trace.hh>
#include <osv/prio.hh>
#include <chrono>

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

void map_read_cached_page(hashkey *key, void *page)
{
    SCOPE_LOCK(read_lock);
    cached_page* pc = new cached_page(*key, page);
    read_cache.emplace(*key, pc);
}

static int create_read_cached_page(vfs_file* fp, hashkey& key)
{
    return fp->read_page_from_cache(&key, key.offset);
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
        // read fault and page is not in write cache yet, return one from ARC, mark it cow
        do {
            if (IS_ZFS(st.st_dev)) {
                WITH_LOCK(arc_read_lock) {
                    cached_page_arc* cp = find_in_cache(arc_read_cache, key);
                    if (cp) {
                        add_arc_read_mapping(cp, ptep);
                        return mmu::write_pte(cp->addr(), ptep, mmu::pte_mark_cow(pte, true));
                    }
                }
            }
            else {
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
    static std::stack<cached_page_write*> dirty; // protected by write_lock
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, 0};
    SCOPE_LOCK(write_lock);

    for (key.offset = start; key.offset < end; key.offset += mmu::page_size) {
        cached_page_write* cp = find_in_cache(write_cache, key);
        if (cp && cp->clear_dirty()) {
            dirty.push(cp);
        }
    }

    mmu::flush_tlb_all();

    while(!dirty.empty()) {
        auto cp = dirty.top();
        auto err = cp->writeback();
        if (err) {
            throw make_error(err);
        }
        dirty.pop();
    }
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
//is loaded.
extern "C" OSV_LIBSOLARIS_API void start_pagecache_access_scanner() {
    pagecache::s_access_scanner = new pagecache::access_scanner();
}
