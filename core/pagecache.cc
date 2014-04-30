/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <boost/variant.hpp>
#include <osv/pagecache.hh>
#include <osv/mempool.hh>
#include <fs/vfs/vfs.h>
#include <osv/trace.hh>
#include <osv/prio.hh>

extern "C" {
void arc_unshare_buf(arc_buf_t*);
void arc_share_buf(arc_buf_t*);
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
    class ptep_flush : public ptes_visitor<int> {
    public:
        ptep_flush(ptep_list& ptes) : ptes_visitor(ptes) {}
        int operator()(std::nullptr_t &v) {
            // nothing to flush
            return 0;
        }
        int operator()(mmu::hw_ptep& ptep) {
            clear_pte(ptep);
            return 1;
        }
        int operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep>>& set) {
            mmu::clear_ptes(set->begin(), set->end());
            return set->size();
        }
    };

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
        ptep_flush flush(_ptes);
        return boost::apply_visitor(flush, _ptes);
    }
    const hashkey& key() {
        return _key;
    }
};

class cached_page_write : public cached_page {
private:
    struct dentry* _dp;
public:
    cached_page_write(hashkey key, vfs_file* fp) : cached_page(key, memory::alloc_page()) {
        _dp = fp->f_dentry;
        dref(_dp);
    }
    ~cached_page_write() {
        if (_page) {
            writeback();
            memory::free_page(_page);
            drele(_dp);
        }
    }
    int writeback()
    {
        struct vnode *vp = _dp->d_vnode;
        int error;
        struct iovec iov {_page, mmu::page_size};
        struct uio uio {&iov, 1, _key.offset, mmu::page_size, UIO_WRITE};

        vn_lock(vp);
        error = VOP_WRITE(vp, &uio, 0);
        vn_unlock(vp);

        return error;
    }
    void* release() { // called to demote a page from cache page to anonymous
        assert(boost::get<std::nullptr_t>(_ptes) == nullptr);
        void *p = _page;
        _page = nullptr;
        drele(_dp);
        return p;
    }
};

class cached_page_arc;

unsigned drop_read_cached_page(cached_page_arc* cp, bool flush = true);

class cached_page_arc : public cached_page {
public:
    typedef std::unordered_multimap<arc_buf_t*, cached_page_arc*> arc_map;

private:
    arc_buf_t* _ab;
    bool _removed = false;

    static arc_map arc_cache_map;

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
}
