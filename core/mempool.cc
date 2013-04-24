#include "mempool.hh"
#include "ilog2.hh"
#include "arch-setup.hh"
#include <cassert>
#include <cstdint>
#include <new>
#include <boost/utility.hpp>
#include <string.h>
#include "libc/libc.hh"
#include "align.hh"
#include <debug.hh>
#include "alloctracker.hh"
#include <atomic>
#include "mmu.hh"

namespace memory {

size_t phys_mem_size;

// Optionally track living allocations, and the call chain which led to each
// allocation. Don't set tracker_enabled before tracker is fully constructed.
alloc_tracker tracker;
bool tracker_enabled = false;
static inline void tracker_remember(void *addr, size_t size)
{
    // Check if tracker_enabled is true, but expect (be quicker in the case)
    // that it is false.
    if (__builtin_expect(tracker_enabled, false)) {
        tracker.remember(addr, size);
    }
}
static inline void tracker_forget(void *addr)
{
    if (__builtin_expect(tracker_enabled, false)) {
        tracker.forget(addr);
    }
}

// Memory allocation strategy
//
// The chief requirement is to be able to deduce the object size.
//
// Small object (< page size) are stored in pages.  The beginning of the page
// contains a header with a pointer to a pool, consisting of all free objects
// of that size.  Small objects are recognized by free() by the fact that
// they are not aligned on a page boundary (since that is occupied by the
// header).  The pool maintains a singly linked list of free objects, and adds
// or frees pages as needed.
//
// Large objects are rounded up to page size.  They have a page-sized header
// in front that contains the page size.  The free list (free_page_ranges)
// is an rbtree sorted by address.  Allocation strategy is first-fit.
//
// Objects that are exactly page sized, and allocated by alloc_page(), come
// from the same pool as large objects, except they don't have a header
// (since we know the size already).

pool::pool(unsigned size)
    : _size(size)
    , _free()
{
    assert(size + sizeof(page_header) <= page_size);
}

pool::~pool()
{
    assert(_free.empty());
}

const size_t pool::max_object_size = page_size - sizeof(pool::page_header);
const size_t pool::min_object_size = sizeof(pool::free_object);

pool::page_header* pool::to_header(free_object* object)
{
    return reinterpret_cast<page_header*>(
                 reinterpret_cast<std::uintptr_t>(object) & ~(page_size - 1));
}

void* pool::alloc()
{
    std::lock_guard<mutex> guard(_lock);
    if (_free.empty()) {
        add_page();
    }
    auto header = _free.begin();
    auto obj = header->local_free;
    ++header->nalloc;
    header->local_free = obj->next;
    if (!header->local_free) {
        _free.erase(header);
    }
    return obj;
}

unsigned pool::get_size()
{
    return _size;
}

void pool::add_page()
{
    void* page = alloc_page();
    auto header = new (page) page_header;
    header->owner = this;
    header->nalloc = 0;
    header->local_free = nullptr;
    for (auto p = page + page_size - _size; p >= header + 1; p -= _size) {
        auto obj = static_cast<free_object*>(p);
        obj->next = header->local_free;
        header->local_free = obj;
    }
    _free.push_back(*header);
}

void pool::free(void* object)
{
    std::lock_guard<mutex> guard(_lock);
    auto obj = static_cast<free_object*>(object);
    auto header = to_header(obj);
    if (!--header->nalloc) {
        if (header->local_free) {
            _free.erase(_free.iterator_to(*header));
        }
        // FIXME: add hysteresis
        free_page(header);
    } else {
        if (!header->local_free) {
            _free.push_front(*header);
        }
        obj->next = header->local_free;
        header->local_free = obj;
    }
}

pool* pool::from_object(void* object)
{
    auto header = to_header(static_cast<free_object*>(object));
    return header->owner;
}

malloc_pool malloc_pools[ilog2_roundup_constexpr(page_size) + 1]
    __attribute__((init_priority(12000)));

malloc_pool::malloc_pool()
    : pool(compute_object_size(this - malloc_pools))
{
}

size_t malloc_pool::compute_object_size(unsigned pos)
{
    size_t size = 1 << pos;
    if (size > max_object_size) {
        size = max_object_size;
    }
    return size;
}

page_range::page_range(size_t _size)
    : size(_size)
{
}

struct addr_cmp {
    bool operator()(const page_range& fpr1, const page_range& fpr2) const {
        return &fpr1 < &fpr2;
    }
};

namespace bi = boost::intrusive;

mutex free_page_ranges_lock;
bi::set<page_range,
        bi::compare<addr_cmp>,
        bi::member_hook<page_range,
                       bi::set_member_hook<>,
                       &page_range::member_hook>
       > free_page_ranges __attribute__((init_priority(12000)));

static void* malloc_large(size_t size)
{
    size = (size + page_size - 1) & ~(page_size - 1);
    size += page_size;

    std::lock_guard<mutex> guard(free_page_ranges_lock);

    for (auto i = free_page_ranges.begin(); i != free_page_ranges.end(); ++i) {
        auto header = &*i;
        page_range* ret_header;
        if (header->size >= size) {
            if (header->size == size) {
                free_page_ranges.erase(i);
                ret_header = header;
            } else {
                void *v = header;
                header->size -= size;
                ret_header = new (v + header->size) page_range(size);
            }
            void* obj = ret_header;
            obj += page_size;
            return obj;
        }
    }
    debug(fmt("malloc_large(): out of memory: can't find %d bytes. aborting.\n")
            % size);
    abort();
}

static page_range* merge(page_range* a, page_range* b)
{
    void* va = a;
    void* vb = b;

    if (va + a->size == vb) {
        a->size += b->size;
        free_page_ranges.erase(*b);
        return a;
    } else {
        return b;
    }
}

// Return a page range back to free_page_ranges. Note how the size of the
// page range is range->size, but its start is at range itself.
static void free_page_range(page_range *range)
{
    std::lock_guard<mutex> guard(free_page_ranges_lock);

    auto i = free_page_ranges.insert(*range).first;
    if (i != free_page_ranges.begin()) {
        i = free_page_ranges.iterator_to(*merge(&*boost::prior(i), &*i));
    }
    if (boost::next(i) != free_page_ranges.end()) {
        merge(&*i, &*boost::next(i));
    }
}

static void free_page_range(void *addr, size_t size)
{
    new (addr) page_range(size);
    free_page_range(static_cast<page_range*>(addr));
}

static void free_large(void* obj)
{
    free_page_range(static_cast<page_range*>(obj - page_size));
}

static unsigned large_object_size(void *obj)
{
    obj -= page_size;
    auto header = static_cast<page_range*>(obj);
    return header->size;
}

void* alloc_page()
{
    std::lock_guard<mutex> guard(free_page_ranges_lock);

    if(free_page_ranges.empty()) {
        debug("alloc_page(): out of memory\n");
        abort();
    }

    auto p = &*free_page_ranges.begin();
    if (p->size == page_size) {
        free_page_ranges.erase(*p);
        tracker_remember(p, page_size);
        return p;
    } else {
        p->size -= page_size;
        void* v = p;
        v += p->size;
        tracker_remember(v, page_size);
        return v;
    }
}

void free_page(void* v)
{
    free_page_range(v, page_size);
    tracker_forget(v);
}

/* Allocate a huge page of a given size N (which must be a power of two)
 * N bytes of contiguous physical memory whose address is a multiple of N.
 * Memory allocated with alloc_huge_page() must be freed with free_huge_page(),
 * not free(), as the memory is not preceded by a header.
 */
void* alloc_huge_page(size_t N)
{
    std::lock_guard<mutex> guard(free_page_ranges_lock);

    for (auto i = free_page_ranges.begin(); i != free_page_ranges.end(); ++i) {
        page_range *range = &*i;
        if (range->size < N)
            continue;
        intptr_t v = (intptr_t) range;
        // Find the the beginning of the last aligned area in the given
        // page range. This will be our return value:
        intptr_t ret = (v+range->size-N) & ~(N-1);
        if (ret<v)
            continue;
        // endsize is the number of bytes in the page range *after* the
        // N bytes we will return. calculate it before changing header->size
        int endsize = v+range->size-ret-N;
        // Make the original page range smaller, pointing to the part before
        // our ret (if there's nothing before, remove this page range)
        if (ret==v)
            free_page_ranges.erase(*range);
        else
            range->size = ret-v;
        // Create a new page range for the endsize part (if there is one)
        if (endsize > 0) {
            void *e = (void *)(ret+N);
            free_page_range(e, endsize);
        }
        // Return the middle 2MB part
        return (void*) ret;
        // TODO: consider using tracker.remember() for each one of the small
        // pages allocated. However, this would be inefficient, and since we
        // only use alloc_huge_page in one place, maybe not worth it.
    }
    // TODO: instead of aborting, tell the caller of this failure and have
    // it fall back to small pages instead.
    debug(fmt("alloc_huge_page: out of memory: can't find %d bytes. aborting.\n") % N);
    abort();
}

void free_huge_page(void* v, size_t N)
{
    free_page_range(v, N);
}

void free_initial_memory_range(void* addr, size_t size)
{
    if (!size) {
        return;
    }
    if (addr == nullptr) {
        ++addr;
        --size;
    }
    auto a = reinterpret_cast<uintptr_t>(addr);
    auto delta = align_up(a, page_size) - a;
    if (delta > size) {
        return;
    }
    addr += delta;
    size -= delta;
    size = align_down(size, page_size);
    if (!size) {
        return;
    }
    free_page_range(addr, size);

}

void  __attribute__((constructor(12001))) setup()
{
    arch_setup_free_memory();
}

void debug_memory_pool(size_t *total, size_t *contig)
{
    *total = *contig = 0;
    std::lock_guard<mutex> guard(free_page_ranges_lock);
    for (auto i = free_page_ranges.begin(); i != free_page_ranges.end(); ++i) {
        auto header = &*i;
        *total += header->size;
        if (header->size > *contig) {
            *contig = header->size;
        }
    }
}

}

extern "C" {
    void* malloc(size_t size);
    void free(void* object);
}

// malloc_large returns a page-aligned object as a marker that it is not
// allocated from a pool.
// FIXME: be less wasteful

static inline void* std_malloc(size_t size)
{
    if ((ssize_t)size < 0)
        return libc_error_ptr<void *>(ENOMEM);
    void *ret;
    if (size <= memory::pool::max_object_size) {
        size = std::max(size, memory::pool::min_object_size);
        unsigned n = ilog2_roundup(size);
        ret = memory::malloc_pools[n].alloc();
    } else {
        ret = memory::malloc_large(size);
    }
    memory::tracker_remember(ret, size);
    return ret;
}

void* calloc(size_t nmemb, size_t size)
{
    // FIXME: check for overflow
    auto n = nmemb * size;
    auto p = malloc(n);
    memset(p, 0, n);
    return p;
}

static size_t object_size(void *object)
{
    if (reinterpret_cast<uintptr_t>(object) & (memory::page_size - 1)) {
        return memory::pool::from_object(object)->get_size();
    } else {
        return memory::large_object_size(object);
    }
}

static inline void* std_realloc(void* object, size_t size)
{
    if (!object)
        return malloc(size);
    if (!size) {
        free(object);
        return NULL;
    }

    size_t old_size = object_size(object);
    size_t copy_size = size > old_size ? old_size : size;
    void* ptr = malloc(size);
    if (ptr) {
        memcpy(ptr, object, copy_size);
        free(object);
    }

    return ptr;
}

static inline void std_free(void* object)
{
    if (!object) {
        return;
    }
    memory::tracker_forget(object);
    if (reinterpret_cast<uintptr_t>(object) & (memory::page_size - 1)) {
        return memory::pool::from_object(object)->free(object);
    } else {
        return memory::free_large(object);
    }
}

namespace dbg {

// debug allocator - give each allocation a new virtual range, so that
// any use-after-free will fault.

bool enabled;

// FIXME: we assume the debug memory space is infinite (which it nearly is)
// and don't reuse space
static const auto debug_base = reinterpret_cast<char*>(0xffffe00000000000);
std::atomic<char*> free_area{debug_base};
struct header {
    explicit header(size_t sz) : size(sz), size2(sz) {
        memset(fence, '$', sizeof fence);
    }
    ~header() {
        assert(size == size2);
        assert(std::all_of(fence, std::end(fence), [=](char c) { return c == '$'; }));
    }
    size_t size;
    char fence[16];
    size_t size2;
};
static const size_t pad_before = mmu::page_size;
static const size_t pad_after = mmu::page_size;

void* malloc(size_t size)
{
    if (!enabled) {
        return std_malloc(size);
    }

    auto hsize = size + sizeof(header);
    auto asize = align_up(hsize, mmu::page_size);
    auto padded_size = pad_before + asize + pad_after;
    void* v = free_area.fetch_add(padded_size, std::memory_order_relaxed);
    v += pad_before;
    mmu::vpopulate(v, asize);
    auto h = new (v) header(size);
    memset(v + hsize, '$', asize - hsize);
    return h + 1;
}

void free(void* v)
{
    if (v < debug_base) {
        return std_free(v);
    }
    auto h = static_cast<header*>(v) - 1;
    auto size = h->size;
    auto hsize = size + sizeof(header);
    auto asize = align_up(hsize, mmu::page_size);
    char* vv = reinterpret_cast<char*>(h);
    assert(std::all_of(vv + hsize, vv  + asize, [=](char c) { return c == '$'; }));
    h->~header();
    mmu::vdepopulate(h, asize);
}

void* realloc(void* v, size_t size)
{
    auto h = static_cast<header*>(v) - 1;
    void* n = malloc(size);
    memcpy(n, v, h->size);
    free(v);
    return n;
}

}

void* malloc(size_t size)
{
#if CONF_debug_memory == 0
    return std_malloc(size);
#else
    return dbg::malloc(size);
#endif
}

void* realloc(void* obj, size_t size)
{
#if CONF_debug_memory == 0
    return std_realloc(obj, size);
#else
    return dbg::realloc(obj, size);
#endif
}

void free(void* obj)
{
#if CONF_debug_memory == 0
    return std_free(obj);
#else
    return dbg::free(obj);
#endif
}

namespace memory {

void enable_debug_allocator()
{
#if CONF_debug_memory == 1
    dbg::enabled = true;
#endif
}

}
