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
#include <osv/trace.hh>
#include <lockfree/ring.hh>
#include <osv/percpu-worker.hh>
#include <preempt-lock.hh>
#include <sched.hh>

TRACEPOINT(trace_memory_malloc, "buf=%p, len=%d", void *, size_t);
TRACEPOINT(trace_memory_free, "buf=%p", void *);
TRACEPOINT(trace_memory_realloc, "in=%p, newlen=%d, out=%p", void *, size_t, void *);

bool smp_allocator = false;

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

//
// Before smp_allocator=true, threads are not yet available. malloc and free
// are used immediately after virtual memory is being initialized.
// sched::cpu::current() uses TLS which is set only later on.
//

static unsigned mempool_cpuid() {
    unsigned c = (smp_allocator ? sched::cpu::current()->id: 0);
    assert(c < 64);
    return c;
}

//
// Since the small pools are managed per-cpu, malloc() always access the correct
// pool on the same CPU that it was issued from, free() on the other hand, may
// happen from different CPUs, so for each CPU, we maintain an array of
// lockless spsc rings, which combined are functioning as huge mpsc ring.
//
// A worker item is in charge of freeing the object from the original
// CPU it was allocated on.
//
// As much as the producer is concerned (cpu who did free()) -
// 1st index -> dest cpu
// 2nd index -> local cpu
//

const unsigned free_objects_ring_size = 256;
typedef ring_spsc<void*, free_objects_ring_size> free_objects_type;
free_objects_type pcpu_free_list[sched::max_cpus][sched::max_cpus];

struct freelist_full_sync_object {
    mutex _mtx;
    condvar _cond;
    void * _free_obj;
};

//
// we use a pcpu sync object to synchronize between the freeing thread and the
// worker item in the edge case of when the above ring is full.
//
// the sync object array performs as a secondary queue with the length of 1
// item (_free_obj), and freeing threads will wait until it was handled by
// the worker item. Their first priority is still to push the object to the
// ring, only if they fail, a single thread may get a hold of,
// _mtx and set _free_obj, all other threads will wait for the worker to drain
// the its ring and this secondary 1-item queue.
//
freelist_full_sync_object freelist_full_sync[sched::max_cpus];

static void free_worker_fn()
{
    unsigned cpu_id = mempool_cpuid();

    // drain the ring, free all objects
    for (unsigned i=0; i < sched::max_cpus; i++) {
        void* obj = nullptr;
        while (pcpu_free_list[cpu_id][i].pop(obj)) {
            memory::pool::from_object(obj)->free(obj);
        }
    }

    // handle secondary 1-item queue.
    // if we have any waiters, wake them up
    auto& sync = freelist_full_sync[cpu_id];
    void* free_obj = nullptr;
    with_lock(sync._mtx, [&] {
        free_obj = sync._free_obj;
        sync._free_obj = nullptr;
    });

    if (free_obj) {
        sync._cond.wake_all();
        memory::pool::from_object(free_obj)->free(free_obj);
    }
}

PCPU_WORKERITEM(free_worker, free_worker_fn);

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
    for (int i=0; i<64; i++)
        assert(_free[i].empty());
}

const size_t pool::max_object_size = page_size - sizeof(pool::page_header);
const size_t pool::min_object_size = sizeof(pool::free_object);

pool::page_header* pool::to_header(free_object* object)
{
    return reinterpret_cast<page_header*>(
                 reinterpret_cast<std::uintptr_t>(object) & ~(page_size - 1));
}

TRACEPOINT(trace_pool_alloc, "this=%p, obj=%p", void*, void*);
TRACEPOINT(trace_pool_free, "this=%p, obj=%p", void*, void*);
TRACEPOINT(trace_pool_free_same_cpu, "this=%p, obj=%p", void*, void*);
TRACEPOINT(trace_pool_free_different_cpu, "this=%p, obj=%p, obj_cpu=%d", void*, void*, unsigned);

void* pool::alloc()
{
    void * ret = nullptr;
    with_lock(preempt_lock, [&] {

        // We enable preemption because add_page() may take a Mutex.
        // this loop ensures we have at least one free page that we can
        // allocate from, in from the context of the current cpu
        unsigned cur_cpu = mempool_cpuid();
        while (_free[cur_cpu].empty()) {
            sched::preempt_enable();
            add_page();
            sched::preempt_disable();
            cur_cpu = mempool_cpuid();
        }

        // We have a free page, get one object and return it to the user
        auto it = _free[cur_cpu].begin();
        page_header *header = &(*it);
        free_object* obj = header->local_free;
        ++header->nalloc;
        header->local_free = obj->next;
        if (!header->local_free) {
            _free[cur_cpu].erase(it);
        }
        ret = obj;
    });

    trace_pool_alloc(this, ret);
    return ret;
}

unsigned pool::get_size()
{
    return _size;
}

static inline void* untracked_alloc_page();
static inline void untracked_free_page(void *v);

void pool::add_page()
{
    // FIXME: this function allocated a page and set it up but on rare cases
    // we may add this page to the free list of a different cpu, due to the
    // enablment of preemption
    void* page = untracked_alloc_page();
    with_lock(preempt_lock, [&] {
        page_header* header = new (page) page_header;
        header->cpu_id = mempool_cpuid();
        header->owner = this;
        header->nalloc = 0;
        header->local_free = nullptr;
        for (auto p = page + page_size - _size; p >= header + 1; p -= _size) {
            auto obj = static_cast<free_object*>(p);
            obj->next = header->local_free;
            header->local_free = obj;
        }
        _free[header->cpu_id].push_back(*header);
    });
}

void pool::free_same_cpu(free_object* obj, unsigned cpu_id)
{
    void* object = static_cast<void*>(obj);
    trace_pool_free_same_cpu(this, object);

    page_header* header = to_header(obj);
    if (!--header->nalloc) {
        if (header->local_free) {
            _free[cpu_id].erase(_free[cpu_id].iterator_to(*header));
        }
        // FIXME: add hysteresis
        sched::preempt_enable();
        untracked_free_page(header);
        sched::preempt_disable();
    } else {
        if (!header->local_free) {
            _free[cpu_id].push_front(*header);
        }
        obj->next = header->local_free;
        header->local_free = obj;
    }
}

void pool::free_different_cpu(free_object* obj, unsigned obj_cpu)
{
    void* object = static_cast<void*>(obj);
    trace_pool_free_different_cpu(this, object, obj_cpu);
    free_objects_type *ring;

    ring = &memory::pcpu_free_list[obj_cpu][mempool_cpuid()];
    if (!ring->push(object)) {
        sched::preempt_enable();

        // The ring is full, take a mutex and use the sync object, hand
        // the object to the secondary 1-item queue
        auto& sync = freelist_full_sync[obj_cpu];
        with_lock(sync._mtx, [&] {
            sync._cond.wait_until(sync._mtx, [&] {
                return (sync._free_obj == nullptr);
            });

            with_lock(preempt_lock, [&] {
                ring = &memory::pcpu_free_list[obj_cpu][mempool_cpuid()];
                if (!ring->push(object)) {
                    // If the ring is full, use the secondary queue.
                    // sync._free_obj is guaranteed null as we're
                    // the only thread which broke out of the cond.wait
                    // loop under the mutex
                    sync._free_obj = object;
                }

                // Wake the worker item in case at least half of the queue is full
                if (ring->size() > free_objects_ring_size/2) {
                    memory::free_worker.signal(sched::cpus[obj_cpu]);
                }
            });
        });

        sched::preempt_disable();
    }
}


void pool::free(void* object)
{
    trace_pool_free(this, object);

    with_lock(preempt_lock, [&] {

        free_object* obj = static_cast<free_object*>(object);
        page_header* header = to_header(obj);
        unsigned obj_cpu = header->cpu_id;
        unsigned cur_cpu = mempool_cpuid();

        if (obj_cpu == cur_cpu) {
            // free from the same CPU this object has been allocated on.
            free_same_cpu(obj, obj_cpu);
        } else {
            // free from a different CPU. we try to hand the buffer
            // to the proper worker item that is pinned to the CPU that this buffer
            // was allocated from, so it'll free it.
            free_different_cpu(obj, obj_cpu);
        }
    });
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

static inline void* untracked_alloc_page()
{
    std::lock_guard<mutex> guard(free_page_ranges_lock);

    if(free_page_ranges.empty()) {
        debug("alloc_page(): out of memory\n");
        abort();
    }

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

void* alloc_page()
{
    void *p = untracked_alloc_page();
    tracker_remember(p, page_size);
    return p;
}

static inline void untracked_free_page(void *v)
{
    free_page_range(v, page_size);
}

void free_page(void* v)
{
    untracked_free_page(v);
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

using mmu::debug_base;
// FIXME: we assume the debug memory space is infinite (which it nearly is)
// and don't reuse space
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
    void* buf = std_malloc(size);
#else
    void* buf = dbg::malloc(size);
#endif

    trace_memory_malloc(buf, size);
    return buf;
}

void* realloc(void* obj, size_t size)
{
#if CONF_debug_memory == 0
    void* buf = std_realloc(obj, size);
#else
    void* buf = dbg::realloc(obj, size);
#endif

    trace_memory_realloc(obj, size, buf);
    return buf;
}

void free(void* obj)
{
    trace_memory_free(obj);

#if CONF_debug_memory == 0
    std_free(obj);
#else
    dbg::free(obj);
#endif
}

namespace memory {

void enable_debug_allocator()
{
#if CONF_debug_memory == 1
    dbg::enabled = true;
#endif
}

void* alloc_phys_contiguous_aligned(size_t size, size_t align)
{
    assert(align <= page_size); // implementation limitation
    assert(is_power_of_two(align));
    // make use of the standard allocator returning page-aligned
    // physically contiguous memory:
    size = std::max(page_size, size);
    return std_malloc(size);
}

void free_phys_contiguous_aligned(void* p)
{
    std_free(p);
}

}
