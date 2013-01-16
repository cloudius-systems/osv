#include "mempool.hh"
#include "ilog2.hh"
#include "arch-setup.hh"
#include <cassert>
#include <cstdint>
#include <new>
#include <boost/utility.hpp>
#include <string.h>
#include "libc/libc.hh"

namespace memory {

size_t phys_mem_size;

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

bi::set<page_range,
        bi::compare<addr_cmp>,
        bi::member_hook<page_range,
                       bi::set_member_hook<>,
                       &page_range::member_hook>
       > free_page_ranges __attribute__((init_priority(12000)));

void* malloc_large(size_t size)
{
    size = (size + page_size - 1) & ~(page_size - 1);
    size += page_size;

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
    abort();
}

page_range* merge(page_range* a, page_range* b)
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

void free_large(void* obj)
{
    obj -= page_size;
    auto header = static_cast<page_range*>(obj);
    auto i = free_page_ranges.insert(*header).first;
    if (i != free_page_ranges.begin()) {
        i = free_page_ranges.iterator_to(*merge(&*boost::prior(i), &*i));
    }
    if (boost::next(i) != free_page_ranges.end()) {
        merge(&*i, &*boost::next(i));
    }
}

unsigned large_object_size(void *obj)
{
    obj -= page_size;
    auto header = static_cast<page_range*>(obj);
    return header->size;
}

void* alloc_page()
{
    assert(!free_page_ranges.empty());
    auto p = &*free_page_ranges.begin();
    if (p->size == page_size) {
        free_page_ranges.erase(*p);
        return p;
    } else {
        p->size -= page_size;
        void* v = p;
        v += p->size;
        return v;
    }
}

void free_page(void* v)
{
    new (v) page_range(page_size);
    free_large(v + page_size);
}

void free_initial_memory_range(uintptr_t addr, size_t size)
{
    if (!size) {
        return;
    }
    // avoid muddling things with null pointers
    if (addr == 0) {
        ++addr;
        --size;
    }
    unsigned delta = ((addr + page_size - 1) & ~(page_size - 1)) - addr;
    if (delta > size) {
        return;
    }
    addr += delta;
    size -= delta;
    size &= ~(page_size - 1);
    if (!size) {
        return;
    }
    void* v = reinterpret_cast<void*>(addr);
    new (v) page_range(size);
    free_large(v + page_size);

}

void  __attribute__((constructor(12001))) setup()
{
    arch_setup_free_memory();
}

}

unsigned ilog2_roundup(size_t n)
{
    // FIXME: optimize
    unsigned i = 0;
    while (n > (size_t(1) << i)) {
        ++i;
    }
    return i;
}

extern "C" {
    void* malloc(size_t size);
    void free(void* object);
}

// malloc_large returns a page-aligned object as a marker that it is not
// allocated from a pool.
// FIXME: be less wasteful

void* malloc(size_t size)
{
    if ((ssize_t)size < 0)
        return libc_error_ptr<void *>(ENOMEM);
    
    if (size <= memory::pool::max_object_size) {
        size = std::max(size, memory::pool::min_object_size);
        unsigned n = ilog2_roundup(size);
        return memory::malloc_pools[n].alloc();
    } else {
        return memory::malloc_large(size);
    }
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

void* realloc(void* object, size_t size)
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

    return NULL;
}

void free(void* object)
{
    if (!object) {
        return;
    }
    if (reinterpret_cast<uintptr_t>(object) & (memory::page_size - 1)) {
        return memory::pool::from_object(object)->free(object);
    } else {
        return memory::free_large(object);
    }
}
