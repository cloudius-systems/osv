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
#include <fs/vfs/vfs.h>
#include <osv/trace.hh>
#include <osv/prio.hh>
#include <chrono>

extern "C" {
void arc_unshare_buf(arc_buf_t*);
void arc_share_buf(arc_buf_t*);
void arc_buf_accessed(const uint64_t[4]);
void arc_buf_get_hashkey(arc_buf_t*, uint64_t[4]);
}

namespace std {
template<>
struct hash<pagecache::hashkey> {
    size_t operator()(const pagecache::hashkey key) const noexcept {
        hash<uint64_t> h;
        return h(key.dev) ^ h(key.ino) ^ h(key.offset);
    }
};

template<> struct hash<mmu::hw_ptep> {
    size_t operator()(const mmu::hw_ptep& ptep) const noexcept {
        hash<const mmu::pt_element*> h;
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

std::unordered_set<mmu::hw_ptep>::iterator begin(std::unique_ptr<std::unordered_set<mmu::hw_ptep>> const& e)
{
    return e->begin();
}

std::unordered_set<mmu::hw_ptep>::iterator end(std::unique_ptr<std::unordered_set<mmu::hw_ptep>> const& e)
{
    return e->end();
}

}

namespace pagecache {

static void* zero_page;

void  __attribute__((constructor(init_prio::pagecache))) setup()
{
    zero_page = memory::alloc_page();
    memset(zero_page, 0, mmu::page_size);
}

class cached_page {
protected:
    const hashkey _key;
    void* _page;
    typedef boost::variant<std::nullptr_t, mmu::hw_ptep, std::unique_ptr<std::unordered_set<mmu::hw_ptep>>> ptep_list;
    ptep_list _ptes; // set of pointers to ptes that map the page

    template<typename T>
    class ptes_visitor : public boost::static_visitor<T> {
    protected:
        ptep_list& _ptes;
    public:
        ptes_visitor(ptep_list& ptes) : _ptes(ptes) {}
    };
    class ptep_add : public ptes_visitor<void> {
        mmu::hw_ptep& _ptep;
    public:
        ptep_add(ptep_list& ptes, mmu::hw_ptep& ptep) : ptes_visitor(ptes), _ptep(ptep) {}
        void operator()(std::nullptr_t& v) {
            _ptes = _ptep;
        }
        void operator()(mmu::hw_ptep& ptep) {
            auto ptes = std::unique_ptr<std::unordered_set<mmu::hw_ptep>>(new std::unordered_set<mmu::hw_ptep>({ptep}));
            ptes->emplace(_ptep);
            _ptes = std::move(ptes);
        }
        void operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep>>& set) {
            set->emplace(_ptep);
        }
    };
    class ptep_remove : public ptes_visitor<int> {
        mmu::hw_ptep& _ptep;
    public:
        ptep_remove(ptep_list& ptes, mmu::hw_ptep& ptep) : ptes_visitor(ptes), _ptep(ptep) {}
        int operator()(std::nullptr_t &v) {
            assert(0);
            return -1;
        }
        int operator()(mmu::hw_ptep& ptep) {
            _ptes = nullptr;
            return 0;
        }
        int operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep>>& set) {
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
        Ret operator()(mmu::hw_ptep& ptep) {
            return _reducer(_initial, _mapper(ptep));
        }
        Ret operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep>>& set) {
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
    ~cached_page() {
    }

    void map(mmu::hw_ptep ptep) {
        ptep_add add(_ptes, ptep);
        boost::apply_visitor(add, _ptes);
    }
    int unmap(mmu::hw_ptep ptep) {
        ptep_remove rm(_ptes, ptep);
        return boost::apply_visitor(rm, _ptes);
    }
    void* addr() {
        return _page;
    }
    int flush() {
        return for_each_pte([] (mmu::hw_ptep pte) { mmu::clear_pte(pte); return 1;});
    }
    int clear_accessed() {
        return for_each_pte([] (mmu::hw_ptep pte) -> int { return mmu::clear_accessed(pte); });
    }
    int clear_dirty() {
        return for_each_pte([] (mmu::hw_ptep pte) -> int { return mmu::clear_dirty(pte); });
    }
    const hashkey& key() {
        return _key;
    }
};

class cached_page_write : public cached_page {
private:
    struct vnode* _vp;
public:
    cached_page_write(hashkey key, vfs_file* fp) : cached_page(key, memory::alloc_page()) {
        _vp = fp->f_dentry->d_vnode;
        vref(_vp);
    }
    ~cached_page_write() {
        if (_page) {
            writeback();
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

        return error;
    }
    void* release() { // called to demote a page from cache page to anonymous
        assert(boost::get<std::nullptr_t>(_ptes) == nullptr);
        void *p = _page;
        _page = nullptr;
        vrele(_vp);
        return p;
    }
};

class cached_page_arc;

unsigned drop_read_cached_page(cached_page_arc* cp, bool flush = true);

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
    ~cached_page_arc() {
        if (!_removed && unref(_ab, this)) {
            arc_unshare_buf(_ab);
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
                count += drop_read_cached_page(cp, false);
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

constexpr unsigned lru_max_length = 100;
constexpr unsigned lru_free_count = 20;

std::unordered_multimap<arc_buf_t*, cached_page_arc*> cached_page_arc::arc_cache_map;
static std::unordered_map<hashkey, cached_page_arc*> read_cache;
static std::unordered_map<hashkey, cached_page_write*> write_cache;
static std::deque<cached_page_write*> write_lru;
static mutex arc_lock; // protects against parallel eviction, parallel creation impossible due to vma_list_lock

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

TRACEPOINT(trace_add_read_mapping, "buf=%p, addr=%p, ptep=%p", void*, void*, void*);
void add_read_mapping(cached_page_arc *cp, mmu::hw_ptep ptep)
{
    trace_add_read_mapping(cp->arcbuf(), cp->addr(), ptep.release());
    cp->map(ptep);
}

TRACEPOINT(trace_remove_mapping, "buf=%p, addr=%p, ptep=%p", void*, void*, void*);
void remove_read_mapping(cached_page_arc* cp, mmu::hw_ptep ptep)
{
    trace_remove_mapping(cp->arcbuf(), cp->addr(), ptep.release());
    if (cp->unmap(ptep) == 0) {
        read_cache.erase(cp->key());
        delete cp;
    }
}

void remove_read_mapping(hashkey& key, mmu::hw_ptep ptep)
{
    SCOPE_LOCK(arc_lock);
    cached_page_arc* cp = find_in_cache(read_cache, key);
    if (cp) {
        remove_read_mapping(cp, ptep);
    }
}

TRACEPOINT(trace_drop_read_cached_page, "buf=%p, addr=%p", void*, void*);
unsigned drop_read_cached_page(cached_page_arc* cp, bool flush)
{
    trace_drop_read_cached_page(cp->arcbuf(), cp->addr());
    int flushed = cp->flush();
    read_cache.erase(cp->key());

    if (flush && flushed > 1) { // if there was only one pte it is the one we are faulting on; no need to flush.
        mmu::flush_tlb_all();
    }

    delete cp;

    return flushed;
}

void drop_read_cached_page(hashkey& key)
{
    SCOPE_LOCK(arc_lock);
    cached_page_arc* cp = find_in_cache(read_cache, key);
    if (cp) {
        drop_read_cached_page(cp, true);
    }
}

TRACEPOINT(trace_unmap_arc_buf, "buf=%p", void*);
void unmap_arc_buf(arc_buf_t* ab)
{
    trace_unmap_arc_buf(ab);
    SCOPE_LOCK(arc_lock);
    cached_page_arc::unmap_arc_buf(ab);
}

TRACEPOINT(trace_map_arc_buf, "buf=%p page=%p", void*, void*);
void map_arc_buf(hashkey *key, arc_buf_t* ab, void *page)
{
    trace_map_arc_buf(ab, page);
    SCOPE_LOCK(arc_lock);
    cached_page_arc* pc = new cached_page_arc(*key, page, ab);
    read_cache.emplace(*key, pc);
    arc_share_buf(ab);
}

static int create_read_cached_page(vfs_file* fp, hashkey& key)
{
    return fp->get_arcbuf(&key, key.offset);
}

static std::unique_ptr<cached_page_write> create_write_cached_page(vfs_file* fp, hashkey& key)
{
    size_t bytes;
    cached_page_write* cp = new cached_page_write(key, fp);
    struct iovec iov {cp->addr(), mmu::page_size};

    assert(sys_read(fp, &iov, 1, key.offset, &bytes) == 0);
    return std::unique_ptr<cached_page_write>(cp);
}

static void insert(cached_page_write* cp) {
    static cached_page_write* tofree[lru_free_count];
    write_cache.emplace(cp->key(), cp);
    write_lru.push_front(cp);

    if (write_lru.size() > lru_max_length) {
        for (unsigned i = 0; i < lru_free_count; i++) {
            cached_page_write *p = write_lru.back();
            write_lru.pop_back();
            write_cache.erase(p->key());
            p->flush();
            tofree[i] = p;
        }
        mmu::flush_tlb_all();
        for (auto p: tofree) {
            delete p;
        }
    }
}

bool get(vfs_file* fp, off_t offset, mmu::hw_ptep ptep, mmu::pt_element pte, bool write, bool shared)
{
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, offset};
    cached_page_write* wcp = find_in_cache(write_cache, key);

    if (write) {
        if (!wcp) {
            auto newcp = create_write_cached_page(fp, key);
            if (shared) {
                // write fault into shared mapping, there page is not in write cache yet, add it.
                wcp = newcp.release();
                insert(wcp);
                // page is moved from ARC to write cache
                // drop ARC page if exists, removing all mappings
                drop_read_cached_page(key);
            } else {
                // remove mapping to ARC page if exists
                remove_read_mapping(key, ptep);
                // cow of private page from ARC
                return mmu::write_pte(newcp->release(), ptep, pte);
            }
        } else if (!shared) {
            // cow of private page from write cache
            void* page = memory::alloc_page();
            memcpy(page, wcp->addr(), mmu::page_size);
            return mmu::write_pte(page, ptep, pte);
        }
    } else if (!wcp) {
        // read fault and page is not in write cache yet, return one from ARC, mark it cow
        do {
            WITH_LOCK(arc_lock) {
                cached_page_arc* cp = find_in_cache(read_cache, key);
                if (cp) {
                    add_read_mapping(cp, ptep);
                    return mmu::write_pte(cp->addr(), ptep, mmu::pte_mark_cow(pte, true));
                }
            }
            // page is not in cache yet, create and try again
        } while (create_read_cached_page(fp, key) != -1);

        // try to access a hole in a file, map by zero_page
        return mmu::write_pte(zero_page, ptep, mmu::pte_mark_cow(pte, true));
    }

    wcp->map(ptep);

    return mmu::write_pte(wcp->addr(), ptep, mmu::pte_mark_cow(pte, !shared));
}

bool release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep ptep)
{
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, offset};
    cached_page_write* wcp = find_in_cache(write_cache, key);

    clear_pte(ptep);

    // page is either in ARC cache or write cache or zero page or private page

    if (wcp && wcp->addr() == addr) {
        // page is in write cache
        wcp->unmap(ptep);
        return false;
    }

    WITH_LOCK(arc_lock) {
        cached_page_arc* rcp = find_in_cache(read_cache, key);
        if (rcp && rcp->addr() == addr) {
            // page is in ARC
            remove_read_mapping(rcp, ptep);
            return false;
        }
    }

    // if a private page, caller will free it
    return addr != zero_page;
}

void sync(vfs_file* fp, off_t start, off_t end)
{
    static std::stack<cached_page_write*> dirty; // protected by vma_list_mutex
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, 0};
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
static class access_scanner {
    static constexpr double _max_cpu = 20;
    static constexpr double _min_cpu = 0.1;
    static constexpr unsigned _freq = 1000;
    double _cpu = _min_cpu;
    sched::thread _thread;
public:
    access_scanner() : _thread(std::bind(&access_scanner::run, this), sched::thread::attr().name("page-access-scanner")) {
        _thread.start();
    }

private:
    bool mark_accessed(std::unordered_set<arc_hashkey>& accessed) {
        if (accessed.empty()) {
            return false;
        }
        for (auto&& arc_hashkey: accessed) {
            arc_buf_accessed(arc_hashkey.key);
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

            WITH_LOCK(arc_lock) {
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
                            arc_buf_get_hashkey(arcbuf, arc_hashkey.key);
                            accessed.emplace(arc_hashkey);
                            cleared++;
                        }
                        scanned++;
                    });
                    current_bucket++;
                    buckets_scanned++;

                    // mark ARC buffers as accessed when we have 1024 of them
                    if (!(cleared % 1024)) {
                        DROP_LOCK(arc_lock) {
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
                _cpu = std::max(_min_cpu, std::min(_max_cpu, _max_cpu * ((cleared*5.0)/scanned)));
            }

            trace_access_scanner(scanned, cleared, _cpu);

            // decay old results a bit
            scanned /= 4;
            cleared /= 2;
        }
    }
} s_access_scanner;

constexpr double access_scanner::_max_cpu;
constexpr double access_scanner::_min_cpu;


}
