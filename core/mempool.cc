/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mempool.hh>
#include <osv/ilog2.hh>
#include "arch-setup.hh"
#include <cassert>
#include <cstdint>
#include <new>
#include <thread>
#include <boost/utility.hpp>
#include <string.h>
#include <lockfree/unordered-queue-mpsc.hh>
#include "libc/libc.hh"
#include <osv/align.hh>
#include <osv/debug.hh>
#include <osv/alloctracker.hh>
#include <atomic>
#include <osv/mmu.hh>
#include <osv/trace.hh>
#include <lockfree/ring.hh>
#include <osv/percpu-worker.hh>
#include <osv/preempt-lock.hh>
#include <osv/sched.hh>
#include <algorithm>
#include <osv/prio.hh>
#include <stdlib.h>
#include <osv/shrinker.h>
#include <osv/defer.hh>
#include "java/jvm_balloon.hh"
#include <boost/dynamic_bitset.hpp>

TRACEPOINT(trace_memory_malloc, "buf=%p, len=%d, align=%d", void *, size_t,
           size_t);
TRACEPOINT(trace_memory_malloc_mempool, "buf=%p, req_len=%d, alloc_len=%d,"
           " align=%d", void*, size_t, size_t, size_t);
TRACEPOINT(trace_memory_malloc_large, "buf=%p, req_len=%d, alloc_len=%d,"
           " align=%d", void*, size_t, size_t, size_t);
TRACEPOINT(trace_memory_malloc_page, "buf=%p, req_len=%d, alloc_len=%d,"
           " align=%d", void*, size_t, size_t, size_t);
TRACEPOINT(trace_memory_free, "buf=%p", void *);
TRACEPOINT(trace_memory_realloc, "in=%p, newlen=%d, out=%p", void *, size_t, void *);
TRACEPOINT(trace_memory_page_alloc, "page=%p", void*);
TRACEPOINT(trace_memory_page_free, "page=%p", void*);
TRACEPOINT(trace_memory_huge_failure, "page ranges=%d", unsigned long);
TRACEPOINT(trace_memory_reclaim, "shrinker %s, target=%d, delta=%d", const char *, long, long);
TRACEPOINT(trace_memory_wait, "allocation size=%d", size_t);

bool smp_allocator = false;
unsigned char *osv_reclaimer_thread;

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

static inline unsigned mempool_cpuid() {
    return (smp_allocator ? sched::cpu::current()->id: 0);
}

static void garbage_collector_fn();
PCPU_WORKERITEM(garbage_collector, garbage_collector_fn);

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

class garbage_sink {
private:
    static const int signal_threshold = 256;
    lockfree::unordered_queue_mpsc<free_object> queue;
    int pushed_since_last_signal {};
public:
    void free(unsigned obj_cpu, free_object* obj)
    {
        queue.push(obj);
        if (++pushed_since_last_signal > signal_threshold) {
            garbage_collector.signal(sched::cpus[obj_cpu]);
            pushed_since_last_signal = 0;
        }
    }

    free_object* pop()
    {
        return queue.pop();
    }
};

static garbage_sink ***pcpu_free_list;

void pool::collect_garbage()
{
    assert(!sched::preemptable());

    unsigned cpu_id = mempool_cpuid();

    for (unsigned i = 0; i < sched::cpus.size(); i++) {
        auto sink = pcpu_free_list[cpu_id][i];
        free_object* obj;
        while ((obj = sink->pop())) {
            memory::pool::from_object(obj)->free_same_cpu(obj, cpu_id);
        }
    }
}

static void garbage_collector_fn()
{
    WITH_LOCK(preempt_lock) {
        pool::collect_garbage();
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
}

// FIXME: handle larger sizes better, while preserving alignment:
const size_t pool::max_object_size = page_size / 4;
const size_t pool::min_object_size = sizeof(free_object);

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
    WITH_LOCK(preempt_lock) {

        // We enable preemption because add_page() may take a Mutex.
        // this loop ensures we have at least one free page that we can
        // allocate from, in from the context of the current cpu
        while (_free->empty()) {
            DROP_LOCK(preempt_lock) {
                add_page();
            }
        }

        // We have a free page, get one object and return it to the user
        auto it = _free->begin();
        page_header *header = &(*it);
        free_object* obj = header->local_free;
        ++header->nalloc;
        header->local_free = obj->next;
        if (!header->local_free) {
            _free->erase(it);
        }
        ret = obj;
    }

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
    WITH_LOCK(preempt_lock) {
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
        _free->push_back(*header);
        if (_free->empty()) {
            /* encountered when starting to enable TLS for AArch64 in mixed
               LE / IE tls models */
            abort();
        }
    }
}

inline bool pool::have_full_pages()
{
    return !_free->empty() && _free->back().nalloc == 0;
}

void pool::free_same_cpu(free_object* obj, unsigned cpu_id)
{
    void* object = static_cast<void*>(obj);
    trace_pool_free_same_cpu(this, object);

    page_header* header = to_header(obj);
    if (!--header->nalloc && have_full_pages()) {
        if (header->local_free) {
            _free->erase(_free->iterator_to(*header));
        }
        DROP_LOCK(preempt_lock) {
            untracked_free_page(header);
        }
    } else {
        if (!header->local_free) {
            if (header->nalloc) {
                _free->push_front(*header);
            } else {
                // keep full pages on the back, so they're not fragmented
                // early, and so we find them easily in have_full_pages()
                _free->push_back(*header);
            }
        }
        obj->next = header->local_free;
        header->local_free = obj;
    }
}

void pool::free_different_cpu(free_object* obj, unsigned obj_cpu, unsigned cur_cpu)
{
    trace_pool_free_different_cpu(this, obj, obj_cpu);
    auto sink = memory::pcpu_free_list[obj_cpu][cur_cpu];
    sink->free(obj_cpu, obj);
}

void pool::free(void* object)
{
    trace_pool_free(this, object);

    WITH_LOCK(preempt_lock) {

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
            free_different_cpu(obj, obj_cpu, cur_cpu);
        }
    }
}

pool* pool::from_object(void* object)
{
    auto header = to_header(static_cast<free_object*>(object));
    return header->owner;
}

class malloc_pool : public pool {
public:
    malloc_pool();
private:
    static size_t compute_object_size(unsigned pos);
};

malloc_pool malloc_pools[ilog2_roundup_constexpr(page_size) + 1]
    __attribute__((init_priority((int)init_prio::malloc_pools)));

struct mark_smp_allocator_intialized {
    mark_smp_allocator_intialized() {
        // FIXME: Handle CPU hot-plugging.
        auto ncpus = sched::cpus.size();
        // Our malloc() is very coarse so allocate all the queues in one large buffer.
        // We allocate at least one page because current implementation of aligned_alloc()
        // is not capable of ensuring aligned allocation for small allocations.
        auto buf = aligned_alloc(alignof(garbage_sink),
                    std::max(page_size, sizeof(garbage_sink) * ncpus * ncpus));
        pcpu_free_list = new garbage_sink**[ncpus];
        for (auto i = 0U; i < ncpus; i++) {
            pcpu_free_list[i] = new garbage_sink*[ncpus];
            for (auto j = 0U; j < ncpus; j++) {
                static_assert(!(sizeof(garbage_sink) %
                        alignof(garbage_sink)), "garbage_sink align");
                auto p = pcpu_free_list[i][j] = static_cast<garbage_sink *>(
                        buf + sizeof(garbage_sink) * (i * ncpus + j));
                new (p) garbage_sink;
            }
        }
        smp_allocator = true;
    }
} s_mark_smp_alllocator_initialized __attribute__((init_priority((int)init_prio::malloc_pools)));

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
                       &page_range::set_hook>
       > free_page_ranges __attribute__((init_priority((int)init_prio::fpranges)));

// Our notion of free memory is "whatever is in the page ranges". Therefore it
// starts at 0, and increases as we add page ranges.
//
// Updates to total should be fairly rare. We only expect updates upon boot,
// and eventually hotplug in an hypothetical future
static std::atomic<size_t> total_memory(0);
static std::atomic<size_t> free_memory(0);
static size_t watermark_lo(0);
static std::atomic<size_t> current_jvm_heap_memory(0);

// At least two (x86) huge pages worth of size;
static size_t constexpr min_emergency_pool_size = 4 << 20;

__thread unsigned emergency_alloc_level = 0;

reclaimer_lock_type reclaimer_lock;

extern "C" void thread_mark_emergency()
{
    emergency_alloc_level = 1;
}

reclaimer reclaimer_thread
    __attribute__((init_priority((int)init_prio::reclaimer)));

void wake_reclaimer()
{
    reclaimer_thread.wake();
}

static void on_free(size_t mem)
{
    free_memory.fetch_add(mem);
}

static void on_alloc(size_t mem)
{
    free_memory.fetch_sub(mem);
    jvm_balloon_adjust_memory(min_emergency_pool_size);
    if ((stats::free() + stats::jvm_heap()) < watermark_lo) {
        reclaimer_thread.wake();
    }
}

static void on_new_memory(size_t mem)
{
    total_memory.fetch_add(mem);
    watermark_lo = stats::total() * 10 / 100;
}

namespace stats {
    size_t free() { return free_memory.load(std::memory_order_relaxed); }
    size_t total() { return total_memory.load(std::memory_order_relaxed); }

    size_t max_no_reclaim()
    {
        auto total = total_memory.load(std::memory_order_relaxed);
        return total - watermark_lo;
    }

    void on_jvm_heap_alloc(size_t mem)
    {
        current_jvm_heap_memory.fetch_add(mem);
        assert(current_jvm_heap_memory.load() < total_memory);
    }
    void on_jvm_heap_free(size_t mem)
    {
        current_jvm_heap_memory.fetch_sub(mem);
    }
    size_t jvm_heap() { return current_jvm_heap_memory.load(); }
}

void reclaimer::wake()
{
    _blocked.wake_one();
}

pressure reclaimer::pressure_level()
{
    assert(mutex_owned(&free_page_ranges_lock));
    if (stats::free() < watermark_lo) {
        return pressure::PRESSURE;
    }
    return pressure::NORMAL;
}

ssize_t reclaimer::bytes_until_normal(pressure curr)
{
    assert(mutex_owned(&free_page_ranges_lock));
    if (curr == pressure::PRESSURE) {
        return watermark_lo - stats::free();
    } else {
        return 0;
    }
}

void oom()
{
    abort("Out of memory: could not reclaim any further. Current memory: %d Kb", stats::free() >> 10);
}

void reclaimer::wait_for_minimum_memory()
{
    if (emergency_alloc_level) {
        return;
    }

    if (stats::free() < min_emergency_pool_size) {
        // Nothing could possibly give us memory back, might as well use up
        // everything in the hopes that we only need a tiny bit more..
        if (!_active_shrinkers) {
            return;
        }
        wait_for_memory(min_emergency_pool_size - stats::free());
    }
}

// Allocating memory here can lead to a stack overflow. That is why we need
// to use boost::intrusive for the waiting lists.
//
// Also, if the reclaimer itself reaches a point in which it needs to wait for
// memory, there is very little hope and we would might as well give up.
void reclaimer::wait_for_memory(size_t mem)
{
    trace_memory_wait(mem);
    _oom_blocked.wait(mem);
}

class page_range_allocator {
public:
    static constexpr unsigned max_order = 16;

    page_range_allocator() : _deferred_free(nullptr) { }

    template<bool UseBitmap = true>
    page_range* alloc(size_t size);
    page_range* alloc_aligned(size_t size, size_t offset, size_t alignment,
                              bool fill = false);
    void free(page_range* pr);

    void initial_add(page_range* pr);

    template<typename Func>
    void for_each(unsigned min_order, Func f);
    template<typename Func>
    void for_each(Func f) {
        for_each<Func>(0, f);
    }

    bool empty() const {
        return _not_empty.none();
    }
    size_t size() const {
        size_t size = _free_huge.size();
        for (auto&& list : _free) {
            size += list.size();
        }
        return size;
    }

private:
    template<bool UseBitmap = true>
    void insert(page_range& pr) {
        auto addr = static_cast<void*>(&pr);
        auto pr_end = static_cast<page_range**>(addr + pr.size - sizeof(page_range**));
        *pr_end = &pr;
        auto order = ilog2(pr.size / page_size);
        if (order >= max_order) {
            _free_huge.insert(pr);
            _not_empty[max_order] = true;
        } else {
            _free[order].push_front(pr);
            _not_empty[order] = true;
        }
        if (UseBitmap) {
            set_bits(pr, true);
        }
    }
    void remove_huge(page_range& pr) {
        _free_huge.erase(_free_huge.iterator_to(pr));
        if (_free_huge.empty()) {
            _not_empty[max_order] = false;
        }
    }
    void remove_list(unsigned order, page_range& pr) {
        _free[order].erase(_free[order].iterator_to(pr));
        if (_free[order].empty()) {
            _not_empty[order] = false;
        }
    }
    void remove(page_range& pr) {
        auto order = ilog2(pr.size / page_size);
        if (order >= max_order) {
            remove_huge(pr);
        } else {
            remove_list(order, pr);
        }
    }

    unsigned get_bitmap_idx(page_range& pr) const {
        auto idx = reinterpret_cast<uintptr_t>(&pr);
        idx -= reinterpret_cast<uintptr_t>(mmu::phys_mem);
        return idx / page_size;
    }
    void set_bits(page_range& pr, bool value, bool fill = false) {
        auto end = pr.size / page_size - 1;
        if (fill) {
            for (unsigned idx = 0; idx <= end; idx++) {
                _bitmap[get_bitmap_idx(pr) + idx] = value;
            }
        } else {
            _bitmap[get_bitmap_idx(pr)] = value;
            _bitmap[get_bitmap_idx(pr) + end] = value;
        }
    }

    bi::multiset<page_range,
                 bi::member_hook<page_range,
                                 bi::set_member_hook<>,
                                 &page_range::set_hook>,
                 bi::constant_time_size<false>> _free_huge;
    bi::list<page_range,
             bi::member_hook<page_range,
                             bi::list_member_hook<>,
                             &page_range::list_hook>,
             bi::constant_time_size<false>> _free[max_order];

    std::bitset<max_order + 1> _not_empty;

    template<typename T>
    class bitmap_allocator {
    public:
        typedef T value_type;
        T* allocate(size_t n);
        void deallocate(T* p, size_t n);
        size_t get_size(size_t n) {
            return align_up(sizeof(T) * n, page_size);
        }
    };
    boost::dynamic_bitset<unsigned long,
                          bitmap_allocator<unsigned long>> _bitmap;
    page_range* _deferred_free;
};

page_range_allocator _free_page_ranges
    __attribute__((init_priority((int)init_prio::fpranges)));

template<typename T>
T* page_range_allocator::bitmap_allocator<T>::allocate(size_t n)
{
    auto pr = _free_page_ranges.alloc<false>(get_size(n));
    return reinterpret_cast<T*>(pr);
}

template<typename T>
void page_range_allocator::bitmap_allocator<T>::deallocate(T* p, size_t n)
{
    auto pr = new (p) page_range(get_size(n));
    assert(!_free_page_ranges._deferred_free);
    _free_page_ranges._deferred_free = pr;
}

template<bool UseBitmap>
page_range* page_range_allocator::alloc(size_t size)
{
    auto exact_order = ilog2_roundup(size / page_size);
    if (exact_order > max_order) {
        exact_order = max_order;
    }
    auto bitset = _not_empty.to_ulong();
    if (exact_order) {
        bitset &= ~((1 << exact_order) - 1);
    }
    auto order = count_trailing_zeros(bitset);

    page_range* range = nullptr;
    if (!bitset) {
        if (!exact_order || _free[exact_order - 1].empty()) {
            return nullptr;
        }
        // TODO: This linear search makes worst case complexity of the allocator
        // O(n). It would be better to fall back to non-contiguous allocation
        // and make worst case complexity depend on the size of requested memory
        // block and the logarithm of the number of free huge page ranges.
        for (auto&& pr : _free[exact_order - 1]) {
            if (pr.size >= size) {
                range = &pr;
                break;
            }
        }
        return nullptr;
    } else if (order == max_order) {
        range = &*_free_huge.rbegin();
        remove_huge(*range);
    } else {
        range = &_free[order].front();
        remove_list(order, *range);
    }

    auto& pr = *range;
    if (pr.size > size) {
        auto& np = *new (static_cast<void*>(&pr) + size)
                        page_range(pr.size - size);
        insert<UseBitmap>(np);
        pr.size = size;
    }
    if (UseBitmap) {
        set_bits(pr, false);
    }
    return &pr;
}

page_range* page_range_allocator::alloc_aligned(size_t size, size_t offset,
                                                size_t alignment, bool fill)
{
    page_range* ret_header = nullptr;
    for_each(std::max(ilog2(size / page_size), 1u) - 1, [&] (page_range& header) {
        char* v = reinterpret_cast<char*>(&header);
        auto expected_ret = v + header.size - size + offset;
        auto alignment_shift = expected_ret - align_down(expected_ret, alignment);
        if (header.size >= size + alignment_shift) {
            remove(header);
            if (alignment_shift) {
                insert(*new (v + header.size - alignment_shift)
                            page_range(alignment_shift));
                header.size -= alignment_shift;
            }
            if (header.size == size) {
                ret_header = &header;
            } else {
                header.size -= size;
                insert(header);
                ret_header = new (v + header.size) page_range(size);
            }
            set_bits(*ret_header, false, fill);
            return false;
        }
        return true;
    });
    return ret_header;
}

void page_range_allocator::free(page_range* pr)
{
    if (_bitmap[get_bitmap_idx(*pr) - 1]) {
        auto pr2 = *(reinterpret_cast<page_range**>(pr) - 1);
        remove(*pr2);
        pr2->size += pr->size;
        pr = pr2;
    }
    if (_bitmap[get_bitmap_idx(*pr) + pr->size / page_size]) {
        auto pr2 = static_cast<page_range*>(static_cast<void*>(pr) + pr->size);
        remove(*pr2);
        pr->size += pr2->size;
    }
    insert(*pr);
}

void page_range_allocator::initial_add(page_range* pr)
{
    auto idx = get_bitmap_idx(*pr) + pr->size / page_size;
    if (idx > _bitmap.size()) {
        auto prev_idx = get_bitmap_idx(*pr) - 1;
        if (_bitmap.size() > prev_idx && _bitmap[prev_idx]) {
            auto pr2 = *(reinterpret_cast<page_range**>(pr) - 1);
            remove(*pr2);
            pr2->size += pr->size;
            pr = pr2;
        }
        insert<false>(*pr);
        _bitmap.reset();
        _bitmap.resize(idx);

        for_each([this] (page_range& pr) { set_bits(pr, true); return true; });
        if (_deferred_free) {
            free(_deferred_free);
            _deferred_free = nullptr;
        }
    } else {
        free(pr);
    }
}

template<typename Func>
void page_range_allocator::for_each(unsigned min_order, Func f)
{
    for (auto& pr : _free_huge) {
        if (!f(pr)) {
            return;
        }
    }
    for (auto order = max_order; order-- > min_order;) {
        for (auto& pr : _free[order]) {
            if (!f(pr)) {
                return;
            }
        }
    }
}

static void* malloc_large(size_t size, size_t alignment)
{
    auto requested_size = size;
    size_t offset;
    if (alignment < page_size) {
        offset = align_up(sizeof(page_range), alignment);
    } else {
        offset = page_size;
    }
    size += offset;
    size = align_up(size, page_size);

    while (true) {
        WITH_LOCK(free_page_ranges_lock) {
            reclaimer_thread.wait_for_minimum_memory();

            for (auto i = free_page_ranges.begin(); i != free_page_ranges.end(); ++i) {
                auto header = &*i;

                char *v = reinterpret_cast<char*>(header);
                auto expected_ret = v + header->size - size + page_size;
                auto alignment_shift = expected_ret -
                        align_down(expected_ret, alignment);

                if (header->size >= size + alignment_shift) {
                    if (alignment_shift) {
                        // Leave "alignment_shift" bytes at the end of the
                        // range free, so our allocation below is aligned.
                        free_page_ranges.insert(*new(v + header->size -
                                alignment_shift) page_range(alignment_shift));
                        header->size -= alignment_shift;
                    }
                    page_range* ret_header;
                    if (header->size == size) {
                        free_page_ranges.erase(i);
                        ret_header = header;
                    } else {
                        header->size -= size;
                        ret_header = new (v + header->size) page_range(size);
                    }
                    on_alloc(size);
                    void* obj = ret_header;
                    obj += offset;
                    trace_memory_malloc_large(obj, requested_size, size,
                                              alignment);
                    return obj;
                }
            }
            reclaimer_thread.wait_for_memory(size);
        }
    }
}

void shrinker::deactivate_shrinker()
{
    reclaimer_thread._active_shrinkers -= _enabled;
    _enabled = 0;
}

void shrinker::activate_shrinker()
{
    reclaimer_thread._active_shrinkers += !_enabled;
    _enabled = 1;
}

shrinker::shrinker(std::string name)
    : _name(name)
{
    // Since we already have to take that lock anyway in pretty much every
    // operation, just reuse it.
    WITH_LOCK(reclaimer_thread._shrinkers_mutex) {
        reclaimer_thread._shrinkers.push_back(this);
        reclaimer_thread._active_shrinkers += 1;
    }
}

extern "C"
void *osv_register_shrinker(const char *name,
                            size_t (*func)(size_t target, bool hard))
{
    return reinterpret_cast<void *>(new c_shrinker(name, func));
}

bool reclaimer_waiters::wake_waiters()
{
    bool woken = false;
    assert(mutex_owned(&free_page_ranges_lock));
    for (auto& fp : free_page_ranges) {
        // We won't do the allocations, so simulate. Otherwise we can have
        // 10Mb available in the whole system, and 4 threads that wait for
        // it waking because they all believe that memory is available
        auto in_this_page_range = fp.size;
        // We expect less waiters than page ranges so the inner loop is one
        // of waiters. But we cut the whole thing short if we're out of them.
        if (_waiters.empty()) {
            return true;
        }

        auto it = _waiters.begin();
        while (it != _waiters.end()) {
            auto& wr = *it;
            it++;

            if (in_this_page_range >= wr.bytes) {
                in_this_page_range -= wr.bytes;
                _waiters.erase(_waiters.iterator_to(wr));
                wr.owner->wake();
                wr.owner = nullptr;
                woken = true;
            }
        }
    }

    if (!_waiters.empty()) {
        reclaimer_thread.wake();
    }
    return woken;
}

// Note for callers: Ideally, we would not only wake, but already allocate
// memory here and pass it back to the waiter. However, memory is not always
// allocated the same way (ex: refill_page_buffer is completely different from
// malloc_large) and that could be cumbersome.
//
// That means that this returning will only mean allocation may succeed, not
// that it will.  Because of that, it is of extreme importance that callers
// pass the exact amount of memory they are waiting for. So for instance, if
// your allocation is 2Mb in size + a 4k header, "bytes" below should be 2Mb +
// 4k, not 2Mb. Failing to do so could livelock the system, that would forever
// wake up believing there is enough memory, when in reality there is not.
void reclaimer_waiters::wait(size_t bytes)
{
    assert(mutex_owned(&free_page_ranges_lock));

    sched::thread *curr = sched::thread::current();

    // Wait for whom?
    if (curr == &reclaimer_thread._thread) {
        oom();
     }

    wait_node wr;
    wr.owner = curr;
    wr.bytes = bytes;
    _waiters.push_back(wr);

    // At this point the reclaimer thread already knows there are waiters,
    // because the _waiters_list was already updated.
    reclaimer_thread.wake();
    sched::thread::wait_until(&free_page_ranges_lock, [&] { return !wr.owner; });
}

reclaimer::reclaimer()
    : _oom_blocked(), _thread([&] { _do_reclaim(); }, sched::thread::attr().detached().name("reclaimer").stack(mmu::page_size))
{
    osv_reclaimer_thread = reinterpret_cast<unsigned char *>(&_thread);
    _thread.start();
}

bool reclaimer::_can_shrink()
{
    auto p = pressure_level();
    // The active fields are protected by the _shrinkers_mutex lock, but there
    // is no need to take it. Worst that can happen is that we either defer
    // this pass, or take an extra pass without need for it.
    if (p == pressure::PRESSURE) {
        return _active_shrinkers != 0;
    }
    return false;
}

void reclaimer::_shrinker_loop(size_t target, std::function<bool ()> hard)
{
    // FIXME: This simple loop works only because we have a single shrinker
    // When we have more, we need to probe them and decide how much to take from
    // each of them.
    WITH_LOCK(_shrinkers_mutex) {
        // We execute this outside the free_page_ranges lock, so the threads
        // freeing memory (or allocating, for that matter) will have the chance
        // to manipulate the free_page_ranges structure.  Executing the
        // shrinkers with the lock held would result in a deadlock.
        for (auto s : _shrinkers) {
            // FIXME: If needed, in the future we can introduce another
            // intermediate threshold that will put is into hard mode even
            // before we have waiters.
            size_t freed = s->request_memory(target, hard());
            trace_memory_reclaim(s->name().c_str(), target, freed);
        }
    }
}

void reclaimer::_do_reclaim()
{
    ssize_t target;
    emergency_alloc_level = 1;

    while (true) {
        WITH_LOCK(free_page_ranges_lock) {
            _blocked.wait(free_page_ranges_lock);
            target = bytes_until_normal();
        }

        // This means that we are currently ballooning, we should
        // try to serve the waiters from temporary memory without
        // going on hard mode. A big batch of more memory is likely
        // in its way.
        if (_oom_blocked.has_waiters() && throttling_needed()) {
            _shrinker_loop(target, [] { return false; });
            WITH_LOCK(free_page_ranges_lock) {
                if (_oom_blocked.wake_waiters()) {
                        continue;
                }
            }
        }

        _shrinker_loop(target, [this] { return _oom_blocked.has_waiters(); });

        WITH_LOCK(free_page_ranges_lock) {
            if (target >= 0) {
                // Wake up all waiters that are waiting and now have a chance to succeed.
                // If we could not wake any, there is nothing really we can do.
                if (!_oom_blocked.wake_waiters()) {
                    oom();
                }
            }

            jvm_balloon_voluntary_return();
        }
    }
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
static void free_page_range_locked(page_range *range)
{
    auto i = free_page_ranges.insert(*range).first;

    on_free(range->size);

    if (i != free_page_ranges.begin()) {
        i = free_page_ranges.iterator_to(*merge(&*boost::prior(i), &*i));
    }
    if (boost::next(i) != free_page_ranges.end()) {
        merge(&*i, &*boost::next(i));
    }
}

// Return a page range back to free_page_ranges. Note how the size of the
// page range is range->size, but its start is at range itself.
static void free_page_range(page_range *range)
{
    WITH_LOCK(free_page_ranges_lock) {
        free_page_range_locked(range);
    }
}

static void free_page_range(void *addr, size_t size)
{
    new (addr) page_range(size);
    free_page_range(static_cast<page_range*>(addr));
}

static void free_large(void* obj)
{
    obj = align_down(obj - 1, page_size);
    free_page_range(static_cast<page_range*>(obj));
}

static unsigned large_object_size(void *obj)
{
    obj = align_down(obj - 1, page_size);
    auto header = static_cast<page_range*>(obj);
    return header->size;
}

struct page_buffer {
    static constexpr size_t max = 512;
    size_t nr = 0;
    void* free[max];
};

PERCPU(page_buffer, percpu_page_buffer);

static void refill_page_buffer()
{
    WITH_LOCK(free_page_ranges_lock) {
        reclaimer_thread.wait_for_minimum_memory();
        if (free_page_ranges.empty()) {
            // That is almost a guaranteed oom, but we can still have some hope
            // if we the current allocation is a small one. Another advantage
            // of waiting here instead of oom'ing directly is that we can have
            // less points in the code where we can oom, and be more
            // predictable.
            reclaimer_thread.wait_for_memory(mmu::page_size);
        }

        auto total_size = 0;
        WITH_LOCK(preempt_lock) {

            auto& pbuf = *percpu_page_buffer;
            auto limit = (pbuf.max + 1) / 2;

            while (pbuf.nr < limit) {
                auto it = free_page_ranges.begin();
                if (it == free_page_ranges.end())
                    break;
                auto p = &*it;
                auto size = std::min(p->size, (limit - pbuf.nr) * page_size);
                p->size -= size;
                total_size += size;
                void* pages = static_cast<void*>(p) + p->size;
                if (!p->size) {
                    free_page_ranges.erase(it);
                }
                while (size) {
                    pbuf.free[pbuf.nr++] = pages;
                    pages += page_size;
                    size -= page_size;
                }
            }
        }
        // That will wake up the reclaimer, we can't do that while holding the preempt_lock
        // condvar's wake() will take a mutex that may sleep, that will require preemption
        // to be enabled.
        on_alloc(total_size);
    }
}

static void unfill_page_buffer()
{
    WITH_LOCK(free_page_ranges_lock) {
        WITH_LOCK(preempt_lock) {
            auto& pbuf = *percpu_page_buffer;

            while (pbuf.nr > pbuf.max / 2) {
                auto v = pbuf.free[--pbuf.nr];
                auto pr = new (v) page_range(page_size);
                free_page_range_locked(pr);
            }
        }
    }
}

static void* alloc_page_local()
{
    WITH_LOCK(preempt_lock) {
        auto& pbuf = *percpu_page_buffer;
        if (!pbuf.nr) {
            return nullptr;
        }
        return pbuf.free[--pbuf.nr];
    }
}

static bool free_page_local(void* v)
{
    WITH_LOCK(preempt_lock) {
        auto& pbuf = *percpu_page_buffer;
        if (pbuf.nr == pbuf.max) {
            return false;
        }
        pbuf.free[pbuf.nr++] = v;
        return true;
    }
}

static void* early_alloc_page()
{
    WITH_LOCK(free_page_ranges_lock) {
        if (free_page_ranges.empty()) {
            debug_early("early_alloc_page(): out of memory\n");
            abort();
        }

        auto begin = free_page_ranges.begin();
        auto p = &*begin;
        p->size -= page_size;
        on_alloc(page_size);
        void* page = static_cast<void*>(p) + p->size;
        if (!p->size) {
            free_page_ranges.erase(begin);
        }
        return page;
    }
}

static void early_free_page(void* v)
{
    auto pr = new (v) page_range(page_size);
    free_page_range(pr);
}

static void* untracked_alloc_page()
{
    void* ret;

    if (!smp_allocator) {
        ret = early_alloc_page();
    } else {
        while (!(ret = alloc_page_local())) {
            refill_page_buffer();
        }
    }
    trace_memory_page_alloc(ret);
    return ret;
}

void* alloc_page()
{
    void *p = untracked_alloc_page();
    tracker_remember(p, page_size);
    return p;
}

static inline void untracked_free_page(void *v)
{
    trace_memory_page_free(v);
    if (!smp_allocator) {
        return early_free_page(v);
    }
    while (!free_page_local(v)) {
        unfill_page_buffer();
    }
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
    WITH_LOCK(free_page_ranges_lock) {
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
            size_t alloc_size;
            if (ret==v) {
                alloc_size = range->size;
                free_page_ranges.erase(i);
            } else {
                // Note that this is is done conditionally because we are
                // operating page ranges. That is what is left on our page
                // ranges, so that is what we bill. It doesn't matter that we
                // are currently allocating "N" bytes.  The difference will be
                // later on wiped by the on_free() call that exists within
                // free_page_range in the conditional right below us.
                alloc_size = range->size - (ret - v);
                range->size = ret-v;
            }
            on_alloc(alloc_size);

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
        // Definitely a sign we are somewhat short on memory. It doesn't *mean* we
        // are, because that might be just fragmentation. But we wake up the reclaimer
        // just to be sure, and if this is not real pressure, it will just go back to
        // sleep
        reclaimer_thread.wake();
        trace_memory_huge_failure(free_page_ranges.size());
        return nullptr;
    }
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

    on_new_memory(size);

    free_page_range(addr, size);

}

void  __attribute__((constructor(init_prio::mempool))) setup()
{
    arch_setup_free_memory();
}

}

extern "C" {
    void* malloc(size_t size);
    void free(void* object);
    size_t malloc_usable_size(void *object);
}

// malloc_large returns a page-aligned object as a marker that it is not
// allocated from a pool.
// FIXME: be less wasteful


static inline void* std_malloc(size_t size, size_t alignment)
{
    if ((ssize_t)size < 0)
        return libc_error_ptr<void *>(ENOMEM);
    void *ret;
    if (size <= memory::pool::max_object_size && alignment <= size && smp_allocator) {
        size = std::max(size, memory::pool::min_object_size);
        unsigned n = ilog2_roundup(size);
        ret = memory::malloc_pools[n].alloc();
        ret = translate_mem_area(mmu::mem_area::main, mmu::mem_area::mempool,
                                 ret);
        trace_memory_malloc_mempool(ret, size, 1 << n, alignment);
    } else if (size <= mmu::page_size && alignment <= mmu::page_size) {
        ret = mmu::translate_mem_area(mmu::mem_area::main, mmu::mem_area::page,
                                       memory::alloc_page());
        trace_memory_malloc_page(ret, size, mmu::page_size, alignment);
    } else {
        ret = memory::malloc_large(size, alignment);
    }
    memory::tracker_remember(ret, size);
    return ret;
}

void* calloc(size_t nmemb, size_t size)
{
    if (nmemb > std::numeric_limits<size_t>::max() / size)
        return nullptr;
    auto n = nmemb * size;
    auto p = malloc(n);
    if (!p)
        return nullptr;
    memset(p, 0, n);
    return p;
}

static size_t object_size(void *object)
{
    switch (mmu::get_mem_area(object)) {
    case mmu::mem_area::main:
        return memory::large_object_size(object);
    case mmu::mem_area::mempool:
        object = mmu::translate_mem_area(mmu::mem_area::mempool,
                                         mmu::mem_area::main, object);
        return memory::pool::from_object(object)->get_size();
    case mmu::mem_area::page:
        return mmu::page_size;
    default:
        abort();
    }
}

static inline void* std_realloc(void* object, size_t size)
{
    if (!object)
        return malloc(size);
    if (!size) {
        free(object);
        return nullptr;
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
    switch (mmu::get_mem_area(object)) {
    case mmu::mem_area::page:
        object = mmu::translate_mem_area(mmu::mem_area::page,
                                         mmu::mem_area::main, object);
        return memory::free_page(object);
    case mmu::mem_area::main:
         return memory::free_large(object);
    case mmu::mem_area::mempool:
        object = mmu::translate_mem_area(mmu::mem_area::mempool,
                                         mmu::mem_area::main, object);
        return memory::pool::from_object(object)->free(object);
    default:
        abort();
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
static const size_t pad_before = 2 * mmu::page_size;
static const size_t pad_after = mmu::page_size;

static __thread bool recursed;

void* malloc(size_t size, size_t alignment)
{
    if (!enabled || recursed) {
        return std_malloc(size, alignment);
    }

    recursed = true;
    auto unrecurse = defer([&] { recursed = false; });

    WITH_LOCK(memory::free_page_ranges_lock) {
        memory::reclaimer_thread.wait_for_minimum_memory();
    }

    // There will be multiple allocations needed to satisfy this allocation; request
    // access to the emergency pool to avoid us holding some lock and then waiting
    // in an internal allocation
    WITH_LOCK(memory::reclaimer_lock) {
        auto asize = align_up(size, mmu::page_size);
        auto padded_size = pad_before + asize + pad_after;
        if (alignment > mmu::page_size) {
            // Our allocations are page-aligned - might need more
            padded_size += alignment - mmu::page_size;
        }
        char* v = free_area.fetch_add(padded_size, std::memory_order_relaxed);
        // change v so that (v + pad_before) is aligned.
        v += align_up(v + pad_before, alignment) - (v + pad_before);
        mmu::vpopulate(v, mmu::page_size);
        new (v) header(size);
        v += pad_before;
        mmu::vpopulate(v, asize);
        memset(v + size, '$', asize - size);
        // fill the memory with garbage, to catch use-before-init
        uint8_t garbage = 3;
        std::generate_n(v, size, [&] { return garbage++; });
        return v;
    }
}

void free(void* v)
{
    if (v < debug_base) {
        return std_free(v);
    }
    assert(!recursed);
    recursed = true;
    auto unrecurse = defer([&] { recursed = false; });
    WITH_LOCK(memory::reclaimer_lock) {
        auto h = static_cast<header*>(v - pad_before);
        auto size = h->size;
        auto asize = align_up(size, mmu::page_size);
        char* vv = reinterpret_cast<char*>(v);
        assert(std::all_of(vv + size, vv + asize, [=](char c) { return c == '$'; }));
        h->~header();
        mmu::vdepopulate(h, mmu::page_size);
        mmu::vdepopulate(v, asize);
        mmu::vcleanup(h, pad_before + asize);
    }
}

void* realloc(void* v, size_t size)
{
    auto alignment = alignof(max_align_t);
    if (alignment > size) {
        alignment = 1ul << ilog2_roundup(size);
    }
    if (!v)
        return malloc(size, alignment);
    if (!size) {
        free(v);
        return nullptr;
    }
    auto h = static_cast<header*>(v - pad_before);
    if (h->size >= size)
        return v;
    void* n = malloc(size, alignment);
    if (!n)
        return nullptr;
    memcpy(n, v, h->size);
    free(v);
    return n;
}

}

void* malloc(size_t size)
{
    static_assert(alignof(max_align_t) >= 2 * sizeof(size_t),
                  "alignof(max_align_t) smaller than glibc alignment guarantee");
    auto alignment = alignof(max_align_t);
    if (alignment > size) {
        alignment = 1ul << ilog2_roundup(size);
    }
#if CONF_debug_memory == 0
    void* buf = std_malloc(size, alignment);
#else
    void* buf = dbg::malloc(size, alignment);
#endif

    trace_memory_malloc(buf, size, alignment);
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

size_t malloc_usable_size(void* obj)
{
    if ( obj == nullptr ) {
        return 0;
    }
    return object_size(obj);
}

// posix_memalign() and C11's aligned_alloc() return an aligned memory block
// that can be freed with an ordinary free(). The following is a temporary
// implementation that simply calls malloc(), aborting when the desired
// alignment has not been achieved. In particular, for large allocations
// our malloc() already returns page-aligned blocks, so such memalign()
// calls will succeed.

int posix_memalign(void **memptr, size_t alignment, size_t size)
{
    // posix_memalign() but not aligned_alloc() adds an additional requirement
    // that alignment is a multiple of sizeof(void*). We don't verify this
    // requirement, and rather always return memory which is aligned at least
    // to sizeof(void*), even if lesser alignment is requested.
    if (!is_power_of_two(alignment)) {
        return EINVAL;
    }
#if CONF_debug_memory == 0
    void* ret = std_malloc(size, alignment);
#else
    void* ret = dbg::malloc(size, alignment);
#endif
    trace_memory_malloc(ret, size, alignment);
    if (!ret) {
        return ENOMEM;
    }
    // Until we have a full implementation, just croak if we didn't get
    // the desired alignment.
    assert (!(reinterpret_cast<uintptr_t>(ret) & (alignment - 1)));
    *memptr = ret;
    return 0;

}

void *aligned_alloc(size_t alignment, size_t size)
{
    void *ret;
    int error = posix_memalign(&ret, alignment, size);
    if (error) {
        errno = error;
        return NULL;
    }
    return ret;
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
    assert(is_power_of_two(align));
    // make use of the standard large allocator returning properly aligned
    // physically contiguous memory:
    auto ret = malloc_large(size, align);
    assert (!(reinterpret_cast<uintptr_t>(ret) & (align - 1)));
    return ret;
}

void free_phys_contiguous_aligned(void* p)
{
    free_large(p);
}

}
