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

namespace mmu {
    extern mutex vma_list_mutex;
};

namespace pagecache {
struct hashkey {
    dev_t dev;
    ino_t ino;
    off_t offset;
    bool operator==(const hashkey& a) const noexcept {
        return (dev == a.dev) && (ino == a.ino) && (offset == a.offset);
    }
};
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

class cached_page {
private:
    const hashkey _key;
    struct dentry* _dp;
    void* _page;
    typedef boost::variant<std::nullptr_t, mmu::hw_ptep, std::unique_ptr<std::unordered_set<mmu::hw_ptep>>> ptep_list;
    ptep_list _ptes; // set of pointers to ptes that map the page

    class ptes_visitor : public boost::static_visitor<> {
    protected:
        ptep_list& _ptes;
    public:
        ptes_visitor(ptep_list& ptes) : _ptes(ptes) {}
    };
    class ptep_add : public ptes_visitor {
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
    class ptep_remove : public ptes_visitor {
        mmu::hw_ptep& _ptep;
    public:
        ptep_remove(ptep_list& ptes, mmu::hw_ptep& ptep) : ptes_visitor(ptes), _ptep(ptep) {}
        void operator()(std::nullptr_t &v) {
            assert(0);
        }
        void operator()(mmu::hw_ptep& ptep) {
            _ptes = nullptr;
        }
        void operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep>>& set) {
            set->erase(_ptep);
            if (set->size() == 1) {
                auto pte = *(set->begin());
                _ptes = pte;
            }
        }
    };
    class ptep_flush : public ptes_visitor {
    public:
        ptep_flush(ptep_list& ptes) : ptes_visitor(ptes) {}
        void operator()(std::nullptr_t &v) {
            // nothing to flush
        }
        void operator()(mmu::hw_ptep& ptep) {
            clear_pte(ptep);
        }
        void operator()(std::unique_ptr<std::unordered_set<mmu::hw_ptep>>& set) {
            mmu::clear_ptes(set->begin(), set->end());
        }
    };

public:
    cached_page(hashkey key, vfs_file* fp) : _key(key) {
        _dp = fp->f_dentry;
        dref(_dp);
        _page = memory::alloc_page();
    }
    ~cached_page() {
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

    void map(mmu::hw_ptep ptep) {
        ptep_add add(_ptes, ptep);
        boost::apply_visitor(add, _ptes);
    }
    void unmap(mmu::hw_ptep ptep) {
        ptep_remove rm(_ptes, ptep);
        boost::apply_visitor(rm, _ptes);
    }
    void* addr() {
        return _page;
    }
    void flush() {
        ptep_flush flush(_ptes);
        boost::apply_visitor(flush, _ptes);
    }
    const hashkey& key() {
        return _key;
    }
    void* release() { // called to demote a page from cache page to anonymous
        assert(boost::get<std::nullptr_t>(_ptes) == nullptr);
        void *p = _page;
        _page = nullptr;
        drele(_dp);
        return p;
    }
};

constexpr unsigned lru_max_length = 100;
constexpr unsigned lru_free_count = 20;

static mutex lock;
static std::unordered_map<hashkey, cached_page*> cache;
static std::deque<cached_page*> lru;

// In the general case, we expect only one element in the list.
static std::unordered_multimap<void *, mmu::hw_ptep> shared_fs_maps;
// We need to reference count the buffer, but first we need to store the
// buffer somewhere we can find
static std::unordered_map<void *, unsigned int> shared_fs_buf_refcnt;
// Can't use the vma_list_mutex, because if we do, we can have a deadlock where
// we call into the filesystem to read data with the vma_list_mutex held - because
// we do that for complex operate operations, and if the filesystem decides to evict
// a page to read the selected buffer, we will need to access those data structures.
static mutex shared_fs_mutex;

static void fs_buf_get(void *buf_addr)
{
    auto b = shared_fs_buf_refcnt.find(buf_addr);
    if (b == shared_fs_buf_refcnt.end()) {
        shared_fs_buf_refcnt.emplace(buf_addr, 1);
        return;
    }
    b->second++;
}

static bool fs_buf_put(void *buf_addr, unsigned dec = 1)
{
    auto b = shared_fs_buf_refcnt.find(buf_addr);
    assert(b != shared_fs_buf_refcnt.end());
    assert(b->second >= dec);
    b->second -= dec;
    if (b->second == 0) {
        shared_fs_buf_refcnt.erase(buf_addr);
        return true;
    }
    return false;
}

TRACEPOINT(trace_add_mapping, "buf=%p, addr=%p, ptep=%p", void*, void*, void*);
void add_mapping(void *buf_addr, void *page, mmu::hw_ptep ptep)
{
    WITH_LOCK(shared_fs_mutex) {
        trace_add_mapping(buf_addr, page, ptep.release());
        shared_fs_maps.emplace(page, ptep);
        fs_buf_get(buf_addr);
    }
}

TRACEPOINT(trace_remove_mapping, "buf=%p, addr=%p, ptep=%p", void*, void*, void*);
bool remove_mapping(void *buf_addr, void *paddr, mmu::hw_ptep ptep)
{
    WITH_LOCK(shared_fs_mutex) {
        auto buf = shared_fs_maps.equal_range(paddr);
        for (auto it = buf.first; it != buf.second; it++) {
            auto stored = (*it).second;
            if (stored == ptep) {
                shared_fs_maps.erase(it);
                trace_remove_mapping(buf_addr, paddr, ptep.release());
                return fs_buf_put(buf_addr);
            }
        }
    }
    return false;
}

bool lookup_mapping(void *paddr, mmu::hw_ptep ptep)
{
    WITH_LOCK(shared_fs_mutex) {
        auto buf = shared_fs_maps.equal_range(paddr);
        for (auto it = buf.first; it != buf.second; it++) {
            auto stored = (*it).second;
            if (stored == ptep) {
                return true;
            }
        }
    }
    return false;
}

TRACEPOINT(trace_unmap_address, "buf=%p, addr=%p, len=%x", void*, void*, uint64_t);
bool unmap_address(void *buf_addr, void *addr, size_t size)
{
    bool last;
    unsigned refs = 0;
    size = align_up(size, mmu::page_size);
    assert(mutex_owned(&mmu::vma_list_mutex));
    WITH_LOCK(shared_fs_mutex) {
        trace_unmap_address(buf_addr, addr, size);
        for (uintptr_t a = reinterpret_cast<uintptr_t>(addr); size; a += mmu::page_size, size -= mmu::page_size) {
            addr = reinterpret_cast<void*>(a);
            auto buf = shared_fs_maps.equal_range(addr);
            refs += clear_ptes(buf.first, buf.second);
            shared_fs_maps.erase(addr);
        }
        last = refs ? fs_buf_put(buf_addr, refs) : false;
    }
    mmu::flush_tlb_all();
    return last;
}

static std::unique_ptr<cached_page> create_write_cached_page(vfs_file* fp, hashkey& key)
{
    size_t bytes;
    cached_page* cp = new cached_page(key, fp);
    struct iovec iov {cp->addr(), mmu::page_size};

    assert(sys_read(fp, &iov, 1, key.offset, &bytes) == 0);
    return std::unique_ptr<cached_page>(cp);
}

static void insert(cached_page* cp) {
    static cached_page* tofree[lru_free_count];
    cache.emplace(cp->key(), cp);
    lru.push_front(cp);

    if (lru.size() > lru_max_length) {
        for (unsigned i = 0; i < lru_free_count; i++) {
            cached_page *p = lru.back();
            lru.pop_back();
            cache.erase(p->key());
            p->flush();
            tofree[i] = p;
        }
        mmu::flush_tlb_all();
        for (auto p: tofree) {
            delete p;
        }
    }
}

static cached_page *find_in_write_cache(hashkey& key)
{
    auto cpi = cache.find(key);

    if (cpi == cache.end()) {
        return nullptr;
    } else {
        return cpi->second;
    }
}

mmu::mmupage get(vfs_file* fp, off_t offset, mmu::hw_ptep ptep, bool write, bool shared)
{
    void *start, *page;
    size_t len;
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, offset};
    SCOPE_LOCK(lock);
    cached_page* cp = find_in_write_cache(key);

    if (write) {
        if (!cp) {
            auto newcp = create_write_cached_page(fp, key);
            // FIXME: if page is not in ARC it will be read here,
            // FIXME: we need a function that return NULL if page is not in ARC
            fp->get_arcbuf(offset, ARC_ACTION_QUERY, &start, &len, &page);
            if (shared) {
                // write fault into shared mapping, there page is not in write cache yet, add it.
                cp = newcp.release();
                insert(cp);
                // page is moved from ARC to write cache
                // remove any mapping to ARC page
                // FIXME: if pte we are changing is the only one, no need to unmap
                if (unmap_address(start, page, mmu::page_size)) {
                    fp->get_arcbuf(offset, ARC_ACTION_RELEASE, &start, &len, &page);
                }
            } else {
                // remove mapping to ARC page if exists
                if (remove_mapping(start, page, ptep)) {
                    fp->get_arcbuf(offset, ARC_ACTION_RELEASE, &start, &len, &page);
                }
                // cow of private page from ARC
                return newcp->release();
            }
        } else if (!shared) {
            // cow of private page from write cache
            page = memory::alloc_page();
            memcpy(page, cp->addr(), mmu::page_size);
            return page;
        }
    } else if (!cp) {
        // read fault and page is not in write cache yet, return one from ARC, mark it cow
        fp->get_arcbuf(offset, ARC_ACTION_HOLD, &start, &len, &page);
        add_mapping(start, page, ptep);
        return mmu::mmupage(page, true);
    }

    cp->map(ptep);
    return cp->addr();
}

bool release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep ptep)
{
    bool free = false;
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, offset};
    SCOPE_LOCK(lock);
    cached_page *cp = find_in_write_cache(key);

    // page is either in ARC cache or write cache or private page
    if (cp && cp->addr() == addr) {
        // page is in write cache
        cp->unmap(ptep);
    } else if (lookup_mapping(addr, ptep)) {
        // page is in ARC
        void *start, *page;
        size_t len;
        fp->get_arcbuf(offset, ARC_ACTION_QUERY, &start, &len, &page);
        assert (addr == page);
        if (remove_mapping(start, page, ptep)) {
            fp->get_arcbuf(offset, ARC_ACTION_RELEASE, &start, &len, &page);
        }
    } else {
        // private page
        free = true;
    }
    return free;
}
}
