/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <unordered_map>
#include <unordered_set>
#include <deque>
#include <osv/pagecache.hh>
#include <osv/mempool.hh>
#include <fs/vfs/vfs.h>

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
    std::unordered_set<mmu::hw_ptep> _ptes; // set of pointers to ptes that map the page
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
        _ptes.emplace(ptep);
    }
    void unmap(mmu::hw_ptep ptep) {
        _ptes.erase(ptep);
    }
    void* addr() {
        return _page;
    }
    void flush() {
        mmu::clear_ptes(_ptes.begin(), _ptes.end());
    }
    const hashkey& key() {
        return _key;
    }
    void* release() { // called to demote a page from cache page to anonymous
        assert(_ptes.size() == 0);
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

static std::unique_ptr<cached_page> create_write_cached_page(vfs_file* fp, hashkey& key)
{
    size_t bytes;
    cached_page* cp = new cached_page(key, fp);
    struct iovec iov {cp->addr(), mmu::page_size};

    sys_read(fp, &iov, 1, key.offset, &bytes);
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
        mmu::tlb_flush();
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
                if (mmu::unmap_address(start, page, mmu::page_size)) {
                    fp->get_arcbuf(offset, ARC_ACTION_RELEASE, &start, &len, &page);
                }
            } else {
                // remove mapping to ARC page if exists
                if (mmu::remove_mapping(start, page, ptep)) {
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
        mmu::add_mapping(start, page, ptep);
        return mmu::mmupage(page, true);
    }

    cp->map(ptep);
    return cp->addr();
}

void release(vfs_file* fp, void *addr, off_t offset, mmu::hw_ptep ptep)
{
    struct stat st;
    fp->stat(&st);
    hashkey key {st.st_dev, st.st_ino, offset};
    SCOPE_LOCK(lock);
    cached_page *cp = find_in_write_cache(key);

    // page is either in ARC cache or write cache or private page
    if (cp && cp->addr() == addr) {
        // page is in write cache
        cp->unmap(ptep);
    } else if (mmu::lookup_mapping(addr, ptep)) {
        // page is in ARC
        void *start, *page;
        size_t len;
        fp->get_arcbuf(offset, ARC_ACTION_QUERY, &start, &len, &page);
        assert (addr == page);
        if (mmu::remove_mapping(start, page, ptep)) {
            fp->get_arcbuf(offset, ARC_ACTION_RELEASE, &start, &len, &page);
        }
    } else {
        // private page
        memory::free_page(addr);
    }
}
}
