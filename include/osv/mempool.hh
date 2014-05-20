/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MEMPOOL_HH
#define MEMPOOL_HH

#include <cstdint>
#include <functional>
#include <list>
#include <boost/intrusive/set.hpp>
#include <boost/intrusive/list.hpp>
#include <osv/mutex.h>
#include <arch.hh>
#include <osv/pagealloc.hh>
#include <osv/percpu.hh>
#include <osv/condvar.h>
#include <osv/semaphore.hh>
#include <osv/mmu.hh>

extern "C" void thread_mark_emergency();

namespace memory {

const size_t page_size = 4096;

extern size_t phys_mem_size;

void* alloc_phys_contiguous_aligned(size_t sz, size_t align);
void free_phys_contiguous_aligned(void* p);

void setup_free_memory(void* start, size_t bytes);

void debug_memory_pool(size_t *total, size_t *contig);

namespace bi = boost::intrusive;

// pre-mempool object smaller than a page
static constexpr size_t non_mempool_obj_offset = 8;

struct free_object {
    free_object* next;
};

class pool {
public:
    explicit pool(unsigned size);
    ~pool();
    void* alloc();
    void free(void* object);
    unsigned get_size();
    static pool* from_object(void* object);
    static void collect_garbage();
private:
    struct page_header;
private:
    bool have_full_pages();
    void add_page();
    static page_header* to_header(free_object* object);

    // should get called with the preemption lock taken
    void free_same_cpu(free_object* obj, unsigned cpu_id);
    void free_different_cpu(free_object* obj, unsigned obj_cpu, unsigned cur_cpu);
private:
    unsigned _size;

    struct page_header {
        pool* owner;
        unsigned cpu_id;
        unsigned nalloc;
        bi::list_member_hook<> free_link;
        free_object* local_free;  // free objects in this page
    };

    static_assert(non_mempool_obj_offset < sizeof(page_header), "non_mempool_obj_offset too large");

    typedef bi::list<page_header,
                     bi::member_hook<page_header,
                                     bi::list_member_hook<>,
                                     &page_header::free_link>,
                     bi::constant_time_size<false>
                    > free_list_base_type;
    class free_list_type : public free_list_base_type {
    public:
        ~free_list_type() { assert(empty()); }
    };
    // maintain a list of free pages percpu
    dynamic_percpu<free_list_type> _free;
public:
    static const size_t max_object_size;
    static const size_t min_object_size;
};

struct page_range {
    explicit page_range(size_t size);
    size_t size;
    boost::intrusive::set_member_hook<> member_hook;
};

void free_initial_memory_range(void* addr, size_t size);
void enable_debug_allocator();

extern bool tracker_enabled;

enum class pressure { RELAXED, NORMAL, PRESSURE, EMERGENCY };

class shrinker {
public:
    explicit shrinker(std::string name);
    virtual ~shrinker() {}
    virtual size_t request_memory(size_t n, bool hard) = 0;
    std::string name() { return _name; };

    bool should_shrink(ssize_t target) { return _enabled && (target > 0); }

    void deactivate_shrinker();
    void activate_shrinker();
private:
    std::string _name;
    int _enabled = 1;
};

class reclaimer_waiters {
public:
    // return true if there were no waiters or if we could wait some. Returns false
    // if there were waiters but we could not wake any of them.
    bool wake_waiters();
    void wait(size_t bytes);
    bool has_waiters() { return !_waiters.empty(); }
private:
    struct wait_node: boost::intrusive::list_base_hook<> {
        sched::thread* owner;
        size_t bytes;
    };
    // Protected by mempool.cc's free_page_ranges_lock
    boost::intrusive::list<wait_node,
                          boost::intrusive::base_hook<wait_node>,
                          boost::intrusive::constant_time_size<false>> _waiters;
};

bool throttling_needed();

void wake_reclaimer();

class reclaimer {
public:
    reclaimer ();
    void wake();
    void wait_for_memory(size_t mem);
    void wait_for_minimum_memory();

    friend void start_reclaimer();
    friend class shrinker;
    friend class reclaimer_waiters;
private:
    void _do_reclaim();
    void _shrinker_loop(size_t target, std::function<bool ()> hard);
    // We could just check if the semaphore's wait_list is empty. But since we
    // don't control the internals of the primitives we use for the
    // implementation of semaphore, we are concerned that unlocked access may
    // mean mayhem. Locked access, OTOH, will possibly deadlock us if someone allocates
    // memory locked, and then we try to verify if we have waiters and need to hold the
    // same lock
    //std::atomic<int> _waiters_count = { 0 };
    //bool _has_waiters() { return _waiters_count.load() != 0; }

    reclaimer_waiters _oom_blocked; // Callers are blocked due to lack of memory
    condvar _blocked;     // The reclaimer itself is blocked waiting for pressure condition
    sched::thread _thread;

    std::vector<shrinker *> _shrinkers;
    mutex _shrinkers_mutex;
    unsigned int _active_shrinkers = 0;
    bool _can_shrink();

    pressure pressure_level();
    ssize_t bytes_until_normal(pressure curr);
    ssize_t bytes_until_normal() { return bytes_until_normal(pressure_level()); }
};

namespace stats {
    size_t free();
    size_t total();
    size_t max_no_reclaim();
    size_t jvm_heap();
    void on_jvm_heap_alloc(size_t mem);
    void on_jvm_heap_free(size_t mem);
}

class phys_contiguous_memory final {
public:
    phys_contiguous_memory(size_t size, size_t align) {
        _va = alloc_phys_contiguous_aligned(size, align);
        if(!_va)
            throw std::bad_alloc();
        _pa = mmu::virt_to_phys(_va);
        _size = size;
    }

    ~phys_contiguous_memory() {
        free_phys_contiguous_aligned(_va);
    }

    void* get_va(void) const { return _va; }
    mmu::phys get_pa(void) const { return _pa; }
    size_t get_size(void) const { return _size; }

private:

    void *_va;
    mmu::phys _pa;
    size_t _size;
};

struct phys_deleter {
    void operator()(void* p) { free_phys_contiguous_aligned(p); }
};

template <typename T>
using phys_ptr = std::unique_ptr<T, memory::phys_deleter>;

template <typename T, size_t align, typename... Args>
inline
phys_ptr<T> make_phys_ptr(Args&&... args)
{
    static_assert(!std::is_array<T>::value, "use make_phys_array() to allocate arrays");
    void* ptr = memory::alloc_phys_contiguous_aligned(sizeof(T), align);
    // we can't put ptr into a phys_ptr<T> until it's fully constructed, otherwise
    // if the constructor throws, we'll run the destructor
    try {
        new (ptr) T(std::forward<Args>(args)...);
    } catch (...) {
        memory::free_phys_contiguous_aligned(ptr);
    }
    return phys_ptr<T>(static_cast<T*>(ptr));
}

template <typename T, size_t align>
inline
phys_ptr<T[]> make_phys_array(size_t n)
{
    // we have nowhere to store n, so we can't run any destructors
    static_assert(std::is_trivially_destructible<T>::value,
            "make_phys_ptr<T[]> must have a trivially destructible type");
    void* ptr = memory::alloc_phys_contiguous_aligned(sizeof(T) * n, align);
    // we can't put ptr into a phys_ptr<T> until it's fully constructed, otherwise
    // if the constructor throws, we'll run the destructor
    try {
        new (ptr) T[n];
    } catch (...) {
        memory::free_phys_contiguous_aligned(ptr);
    }
    return phys_ptr<T[]>(static_cast<T*>(ptr));
}

template <typename T>
inline
mmu::phys
virt_to_phys(const phys_ptr<T>& p)
{
    return mmu::virt_to_phys_dynamic_phys(p.get());
}


extern __thread unsigned emergency_alloc_level;

class reclaimer_lock_type {
public:
    static void lock() { ++emergency_alloc_level; }
    static void unlock() { --emergency_alloc_level; }
};

/// Hold to mark self as a memory reclaimer
extern reclaimer_lock_type reclaimer_lock;

}

#endif
