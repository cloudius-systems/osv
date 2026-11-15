/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/kernel_config_fork.h>

#if CONF_fork

#include <osv/fork_arena.hh>
#include <osv/mmu.hh>
#include <osv/mmu-defs.hh>
#include <osv/align.hh>
#include <osv/debug.hh>
#include <osv/spinlock.h>
#include <osv/mutex.h>
#include <atomic>
#include <cerrno>
#include <new>
#include <osv/sched.hh>

// -----------------------------------------------------------------------------
// Fork heap arena implementation.  See include/osv/fork_arena.hh for the why.
//
// Layout of a served chunk:
//     [ chunk base (8B) ][ chunk_header (8B) ][ user data ... ]
//                                            ^ returned pointer (16-aligned)
// The header records the size class so free()/usable_size() need no external
// bookkeeping.  Both metadata words sit immediately BELOW the returned pointer,
// so recovering them is two loads at fixed negative offsets with no lookup.
//
// Ownership.  The arena range [arena_base, arena_end) is cut into 64 MiB
// extents.  owner[e] names the per-process state (as_arena) of the address
// space that owns extent e, or is null while e is free; it is identity memory,
// so free() decides "mine or not" without reading the arena: a free whose
// extent is not owned by the freeing address space is a no-op.  An entry is
// set only while its address space lives and is cleared before that state is
// freed, so a live state never matches a stale entry.
//
// Placement.  An address space carves fresh chunks, in 1 MiB mapped regions,
// from extents it owns, and recycles freed chunks through per-class free
// stacks.  Freed extents go back to a FIFO pool.  A dead process's extent can
// still be mapped, by inheritance, in its live descendants, so an address
// space never takes an extent it maps any part of (mmu::intersects_vma).
//
// Locks.  The growth mutex serializes an address space's mapping of new
// regions; the pool spinlock covers identity-memory pool links only.  No arena
// memory is touched under a spinlock, and alloc/free with preemption or irqs
// off never touch arena memory: arena writes may COW-fault.
// -----------------------------------------------------------------------------

namespace fork_arena {

volatile __thread unsigned force_kernel_heap = 0;

namespace {

// Size classes.  NOT powers of two: a pure power-of-2 table wastes up to 50% of
// every class by construction, and combined with the header it pushed small
// requests a whole class up (malloc(40) needed 72 bytes -> the 128 class, 68.8%
// of it not user data).  A PostgreSQL parse node that should fit one cache line
// then occupied two classes' worth of footprint, doubling the working set of
// exactly the allocation-heavy scattered-access code that measured ~2x.
//
// Instead, the progression libumem/jemalloc use:
//   idx  0..7    16 32 48 64 80 96 112 128         (16-byte granule)
//   idx  8..63    160 192 224 256 320 ... 2 MiB    (4 steps per octave)
// Every class is a multiple of 16 (so a chunk base is always 16-aligned, which
// is what malloc's alignof(max_align_t) contract needs), strictly increasing,
// and the last class is exactly max_alloc.  Worst-case internal waste from the
// table is 20% above 128 bytes, and one 16-byte granule below it.
constexpr unsigned num_classes = 64;
constexpr unsigned linear_classes = 8;      // idx 0..7 are 16*(idx+1)
constexpr size_t   granule = 16;            // linear step, and every class is a multiple
constexpr size_t   linear_max = linear_classes * granule;   // 128
constexpr unsigned linear_max_shift = 7;                    // 1<<7 == linear_max

struct chunk_header {
    uint32_t class_idx;   // index into the size-class table (class_size_for)
    uint32_t magic;
};
constexpr uint32_t chunk_magic = 0x464b4152;   // "FKAR"
constexpr uint32_t free_magic  = 0x46524545;   // "FREE": a double free asserts

// Reserved bytes between the chunk base and the returned pointer.  Exactly the
// two metadata words that live there: the saved chunk base at [user-16,user-8)
// and the chunk_header at [user-8,user).  16 is also the minimum that keeps the
// returned pointer 16-aligned when the chunk base is, so nothing is gained by
// shrinking it further -- an 8-byte header would still have to round the user
// pointer up to base+16 and would occupy an identical class.
constexpr size_t header_size = 16;
static_assert(header_size >= sizeof(chunk_header) + sizeof(uintptr_t),
              "header_size must hold the chunk_header and the saved chunk base");
static_assert(header_size % 16 == 0,
              "header_size must preserve 16-byte alignment of the user pointer");

// A region is the unit of mapping: map_anon(mmap_fixed | mmap_populate) of
// this much (or one chunk, if larger) inside an owned extent.  Eager
// population commits the whole region, so it is kept small: about 1 MiB per
// live process, amortized over ~64 average chunks.
constexpr size_t region_sz = 1ull << 20;
static_assert(extent_size % region_sz == 0, "regions tile an extent");

constexpr uint32_t no_extent = ~0u;
static_assert(num_extents < no_extent, "extent indices fit in 32 bits");

// Free-stack head word: chunk pointer >> 4 (chunks are 16-aligned and below
// 2^47, so 43 bits) and a 21-bit tag that makes a pop whose `next` read raced
// with another pop fail its CAS.
constexpr unsigned ptr_bits = 43;
constexpr uint64_t ptr_mask = (uint64_t(1) << ptr_bits) - 1;
static_assert(arena_end <= (uint64_t(1) << (ptr_bits + 4)), "arena VA fits the head word");
inline uintptr_t head_ptr(uint64_t h) { return uintptr_t(h & ptr_mask) << 4; }
inline uint64_t head_make(uintptr_t p, uint64_t old)
{
    return ((uint64_t(p) >> 4) & ptr_mask) | (((old >> ptr_bits) + 1) << ptr_bits);
}

// Per-address-space state.  Identity memory, found through the address
// space (mmu::fork_arena_state), freed by release_as().
struct as_arena {
    explicit as_arena(void *a) : as(a) {
        for (auto &h : heads) {
            h.store(0, std::memory_order_relaxed);
        }
    }
    void *const as;                          // the owning mmu::address_space
    std::atomic<uint64_t> heads[num_classes];
    std::atomic<uintptr_t> next{0};          // carve cursor in the current region
    std::atomic<uintptr_t> end{0};           // end of the current region (0 = none)
    mutex grow_lock;                         // serializes growth, never the fast carve
    // Under grow_lock:
    uint32_t chain = no_extent;              // this AS's extents, linked by g_link
    uint32_t cur = no_extent;                // extent being carved
    size_t cur_used = 0;                     // bytes of it mapped so far
    size_t committed = 0;                    // region bytes mapped in this AS
};

std::atomic<bool> g_ready{false};

// Extent tables, identity memory allocated once in init().
std::atomic<as_arena*> *g_owner;   // owning state, or null when free
std::atomic<uint8_t> *g_used;      // MiB of the extent its owner has mapped
uint32_t *g_link;                  // next extent in an AS's chain or in the pool
static_assert((extent_size >> 20) <= 255, "g_used holds an extent's MiB");

// The pool: extents released by dead address spaces, FIFO, plus the
// never-used extents [g_fresh, num_extents).
spinlock g_pool_lock;
uint32_t g_pool_head = no_extent, g_pool_tail = no_extent;
size_t g_pool_count;
uint32_t g_fresh;

std::atomic<unsigned long> g_live_states{0};
std::atomic<unsigned long> g_extents_in_use{0};
std::atomic<unsigned long> g_committed{0};
std::atomic<unsigned long> g_foreign_frees{0};
std::atomic<unsigned long> g_atomic_retained{0};

inline uintptr_t extent_addr(uint32_t e)
{
    return arena_base + (uintptr_t(e) << extent_shift);
}

inline uint32_t extent_of(const void *p)
{
    return uint32_t((reinterpret_cast<uintptr_t>(p) - arena_base) >> extent_shift);
}

// Byte size of size class @idx.  See the class-table comment above.
constexpr size_t class_size_for(unsigned idx)
{
    if (idx < linear_classes) {
        return granule * (idx + 1);
    }
    unsigned octave = (idx - linear_classes) / 4;   // 0 -> (128,256], 1 -> (256,512], ...
    unsigned step   = (idx - linear_classes) % 4;   // quarter within the octave
    size_t base = linear_max << octave;
    return base + (base >> 2) * (step + 1);
}

// Smallest class index whose class_size_for() is >= @total.  O(1): one clz plus
// shifts, where the old power-of-2 version ran a shift-and-compare loop.
// @total must be >= 1 and <= max_alloc.
constexpr unsigned class_for(size_t total)
{
    if (total <= linear_max) {
        return unsigned((total + granule - 1) / granule) - 1;
    }
    // Octave of the value: bit index of the top set bit of (total-1), so an
    // exact power of two stays in its own octave rather than starting the next.
    unsigned lg = 63u - unsigned(__builtin_clzll(total - 1));
    // Which quarter of (1<<lg, 1<<(lg+1)] the value falls in.
    unsigned step = unsigned(((total - 1) >> (lg - 2)) & 3u);
    return linear_classes + (lg - linear_max_shift) * 4u + step;
}

static_assert(class_size_for(0) == granule, "first class is one granule");
static_assert(class_size_for(linear_classes - 1) == linear_max, "linear range ends at linear_max");
static_assert(class_size_for(num_classes - 1) == max_alloc, "top class is exactly max_alloc");

// Compile-time proof that the two halves of the table agree, over EVERY class.
// This lives here rather than in a test so the formulas cannot drift apart: any
// edit to class_for() or class_size_for() that breaks monotonicity, the 16-byte
// granularity, or the size<->index round trip fails the build.
constexpr bool class_table_consistent()
{
    for (unsigned i = 0; i < num_classes; i++) {
        size_t c = class_size_for(i);
        if (c % granule != 0) {
            return false;                     // chunk bases must stay 16-aligned
        }
        if (class_for(c) != i) {
            return false;                     // an exact fit must map to its own class
        }
        if (i > 0) {
            size_t prev = class_size_for(i - 1);
            if (c <= prev) {
                return false;                 // strictly increasing
            }
            if (class_for(prev + 1) != i) {
                return false;                 // one byte over the previous class lands here
            }
        }
    }
    return class_for(1) == 0;                 // smallest request uses the smallest class
}
static_assert(class_table_consistent(), "fork_arena size-class table is inconsistent");

} // anonymous namespace

void init()
{
    if (g_ready.load(std::memory_order_acquire)) {
        return;
    }
    kernel_heap_scope kh;
    g_owner = new std::atomic<as_arena*>[num_extents]();
    g_used = new std::atomic<uint8_t>[num_extents]();
    g_link = new uint32_t[num_extents];
    g_ready.store(true, std::memory_order_release);
}

bool ready()
{
    return g_ready.load(std::memory_order_acquire);
}

namespace {

void pool_push_tail(uint32_t e)
{
    SCOPE_LOCK(g_pool_lock);
    g_link[e] = no_extent;
    if (g_pool_tail == no_extent) {
        g_pool_head = e;
    } else {
        g_link[g_pool_tail] = e;
    }
    g_pool_tail = e;
    g_pool_count++;
}

uint32_t pool_pop_head()
{
    SCOPE_LOCK(g_pool_lock);
    uint32_t e = g_pool_head;
    if (e != no_extent) {
        g_pool_head = g_link[e];
        if (g_pool_head == no_extent) {
            g_pool_tail = no_extent;
        }
        g_pool_count--;
    }
    return e;
}

uint32_t take_fresh()
{
    SCOPE_LOCK(g_pool_lock);
    return g_fresh < num_extents ? g_fresh++ : no_extent;
}

// Can address space (the current one) take extent @e?  Not if it maps any part
// of it: that is memory it inherited from the extent's previous owner.
bool usable(uint32_t e)
{
    return !mmu::intersects_vma(extent_addr(e), extent_addr(e) + extent_size);
}

// A new extent for @st, in the current address space, under st->grow_lock.
// A child reuses a released extent first, the oldest it may take.  AS0, where
// the kernel's own threads run, takes never-used extents first, so a late
// kernel-thread free of a dead child's pointer cannot match an extent AS0
// owns; it reuses released ones only once every extent has been used.
uint32_t acquire_extent(as_arena *st)
{
    bool as0 = st->as == mmu::kernel_address_space();
    uint32_t e = no_extent;
    if (as0) {
        while ((e = take_fresh()) != no_extent && !usable(e)) {
            pool_push_tail(e);
        }
    }
    if (e == no_extent) {
        size_t n;
        WITH_LOCK(g_pool_lock) {
            n = g_pool_count;
        }
        while (n-- && (e = pool_pop_head()) != no_extent && !usable(e)) {
            pool_push_tail(e);
            e = no_extent;
        }
    }
    if (e == no_extent && !as0) {
        while ((e = take_fresh()) != no_extent && !usable(e)) {
            pool_push_tail(e);
        }
    }
    if (e == no_extent) {
        return no_extent;
    }
    g_used[e].store(0, std::memory_order_relaxed);
    g_owner[e].store(st, std::memory_order_release);
    g_link[e] = st->chain;
    st->chain = e;
    g_extents_in_use.fetch_add(1, std::memory_order_relaxed);
    return e;
}

as_arena *current_state()
{
    auto &slot = mmu::fork_arena_state(mmu::current_address_space());
    return static_cast<as_arena*>(slot.load(std::memory_order_acquire));
}

as_arena *state_for_alloc()
{
    auto as = mmu::current_address_space();
    auto &slot = mmu::fork_arena_state(as);
    auto st = static_cast<as_arena*>(slot.load(std::memory_order_acquire));
    if (st) {
        return st;
    }
    as_arena *n;
    {
        kernel_heap_scope kh;
        n = new (std::nothrow) as_arena(as);
    }
    if (!n) {
        return nullptr;
    }
    void *expected = nullptr;
    if (slot.compare_exchange_strong(expected, n, std::memory_order_acq_rel,
                                     std::memory_order_acquire)) {
        g_live_states.fetch_add(1, std::memory_order_relaxed);
        return n;
    }
    {
        kernel_heap_scope kh;
        delete n;
    }
    return static_cast<as_arena*>(expected);
}

// Read end before next: observing a newly published end also observes its
// initial next.  Within one extent regions are mapped in increasing order and
// next only grows, so a stale end cannot authorize a carve past a newer
// region's end.  An address space never returns to an extent it has left, so
// a cursor and an end in the same extent always describe one region.
void *carve(as_arena *st, size_t class_size)
{
    for (;;) {
        uintptr_t end = st->end.load(std::memory_order_acquire);
        uintptr_t c = st->next.load(std::memory_order_relaxed);
        if (!end || c > end || class_size > end - c ||
                ((c ^ (end - 1)) >> extent_shift)) {
            return nullptr;
        }
        if (st->next.compare_exchange_weak(c, c + class_size,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return reinterpret_cast<void*>(c);
        }
    }
}

void *grow(as_arena *st, size_t class_size)
{
    // Covers mutex waiter allocation and map_anon's nested allocations.
    kernel_heap_scope kh;
    SCOPE_LOCK(st->grow_lock);
    if (void *chunk = carve(st, class_size)) {
        return chunk;
    }
    size_t rsz = align_up(class_size, region_sz);
    if (st->cur == no_extent || rsz > extent_size - st->cur_used) {
        // The tail of the current extent, if any, is left unused.
        uint32_t e = acquire_extent(st);
        if (e == no_extent) {
            return nullptr;
        }
        st->cur = e;
        st->cur_used = 0;
    }
    uintptr_t want = extent_addr(st->cur) + st->cur_used;
    void *v = mmu::map_anon(reinterpret_cast<void*>(want), rsz,
                            mmu::mmap_fixed | mmu::mmap_populate, mmu::perm_rw);
    if (reinterpret_cast<uintptr_t>(v) != want) {
        if (v) {
            mmu::munmap(v, rsz);
        }
        return nullptr;   // nothing published, so a later call can retry
    }
    st->cur_used += rsz;
    st->committed += rsz;
    g_used[st->cur].store(st->cur_used >> 20, std::memory_order_release);
    g_committed.fetch_add(rsz, std::memory_order_relaxed);
    st->next.store(want + class_size, std::memory_order_relaxed);
    st->end.store(want + rsz, std::memory_order_release);
    return v;
}

// The chunk is in this address space, populated, and never unmapped while
// it lives, so reading its link cannot fault; if another pop took it first
// the link may be garbage, and the tag fails the CAS.
void *pop(as_arena *st, unsigned idx)
{
    auto &head = st->heads[idx];
    uint64_t h = head.load(std::memory_order_acquire);
    for (;;) {
        uintptr_t c = head_ptr(h);
        if (!c) {
            return nullptr;
        }
        uintptr_t n = __atomic_load_n(reinterpret_cast<uintptr_t*>(c), __ATOMIC_RELAXED);
        if (head.compare_exchange_weak(h, head_make(n, h),
                std::memory_order_acquire, std::memory_order_acquire)) {
            return reinterpret_cast<void*>(c);
        }
    }
}

// Writes the chunk, which may COW-fault: preemptible, and no lock held.
void push(as_arena *st, uintptr_t chunk, unsigned idx)
{
    auto &head = st->heads[idx];
    uint64_t h = head.load(std::memory_order_relaxed);
    for (;;) {
        __atomic_store_n(reinterpret_cast<uintptr_t*>(chunk), head_ptr(h), __ATOMIC_RELAXED);
        if (head.compare_exchange_weak(h, head_make(chunk, h),
                std::memory_order_release, std::memory_order_relaxed)) {
            return;
        }
    }
}

// Bytes a request occupies in a chunk (header + alignment padding + user
// bytes), or 0 if the arena does not serve it.  The returned pointer is
// header_size past the chunk start when alignment divides header_size;
// otherwise it is aligned up within the chunk.
size_t chunk_need(size_t size, size_t alignment)
{
    if (size > max_alloc || alignment > max_alloc) {
        return 0;
    }
    if (alignment < 16) {
        alignment = 16;
    }
    if (!size) {
        size = 1;   // malloc(0) still gets a distinct pointer with usable bytes
    }
    size_t need = header_size + size + (alignment > header_size ? alignment : 0);
    return need <= max_alloc ? need : 0;
}

inline void recover(void *p, uintptr_t &base, unsigned &idx)
{
    uintptr_t user = reinterpret_cast<uintptr_t>(p);
    auto *h = reinterpret_cast<chunk_header*>(user - sizeof(chunk_header));
    assert(h->magic == chunk_magic);
    idx = h->class_idx;
    assert(idx < num_classes);
    base = *reinterpret_cast<uintptr_t*>(user - 16);
}

} // anonymous namespace

bool serves(size_t size, size_t alignment)
{
    return chunk_need(size, alignment) != 0;
}

void *alloc(size_t size, size_t alignment)
{
    // Even populated pages may be read-only after fork, including in the
    // parent. Neither recycled nor fresh chunks are safe to write here.
    if (!sched::preemptable() || !arch::irq_enabled()) {
        return nullptr;
    }
    if (!g_ready.load(std::memory_order_acquire)) {
        return nullptr;
    }
    size_t need = chunk_need(size, alignment);
    if (!need) {
        return nullptr;
    }
    if (alignment < 16) {
        alignment = 16;
    }
    unsigned idx = class_for(need);
    size_t class_size = class_size_for(idx);

    as_arena *st = state_for_alloc();
    if (!st) {
        errno = ENOMEM;
        return nullptr;
    }
    void *chunk = pop(st, idx);
    if (!chunk) {
        chunk = carve(st, class_size);
    }
    if (!chunk) {
        chunk = grow(st, class_size);
    }
    if (!chunk) {
        errno = ENOMEM;
        return nullptr;
    }

    // The chunk is uniquely ours, so these writes race with nobody; a fresh
    // chunk is populated, a recycled one may COW-fault (preemption is on).
    uintptr_t base = reinterpret_cast<uintptr_t>(chunk);
    uintptr_t user = align_up(base + header_size, alignment);
    auto *h = reinterpret_cast<chunk_header*>(user - sizeof(chunk_header));
    h->class_idx = idx;
    h->magic = chunk_magic;
    // Store the chunk base so free() can reconstruct it regardless of the
    // alignment padding: [base .. user) is ours, and user - 16 >= base.
    *reinterpret_cast<uintptr_t*>(user - 16) = base;
    return reinterpret_cast<void*>(user);
}

void free(void *p)
{
    // No arena access in atomic context: population does not imply that an
    // inherited mapping is safely writable here. Retain until AS teardown.
    if (!sched::preemptable() || !arch::irq_enabled()) {
        g_atomic_retained.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    if (!g_ready.load(std::memory_order_acquire) || !contains(p)) {
        return;
    }
    // Ownership from identity memory only: a free from any other address
    // space (the reaper, RCU, a sibling, a child freeing what it inherited)
    // reads no arena memory and changes nothing.
    // The mapped high-water also keeps a stale free into an extent this
    // address space owns, but has not mapped that far, from reading it.
    as_arena *st = current_state();
    uint32_t e = extent_of(p);
    if (!st || g_owner[e].load(std::memory_order_acquire) != st ||
            reinterpret_cast<uintptr_t>(p) - extent_addr(e) >=
            (uintptr_t(g_used[e].load(std::memory_order_acquire)) << 20)) {
        g_foreign_frees.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    uintptr_t base;
    unsigned idx;
    recover(p, base, idx);   // requires an intact resident same-AS allocation
    reinterpret_cast<chunk_header*>(reinterpret_cast<uintptr_t>(p) -
                                    sizeof(chunk_header))->magic = free_magic;
    push(st, base, idx);
}

size_t usable_size(void *p)
{
    // Same-AS realloc / malloc_usable_size requires an intact resident
    // allocation, including its header. Ordinary fork preserves readability;
    // absent/protected mappings and foreign pointers are not covered.
    uintptr_t base;
    unsigned idx;
    recover(p, base, idx);
    size_t class_size = class_size_for(idx);
    uintptr_t user = reinterpret_cast<uintptr_t>(p);
    // Usable bytes = from user pointer to end of the chunk.
    return class_size - (user - base);
}

void release_as(void *as)
{
    // Called from mmu::destroy_address_space once every user of @as is gone
    // and its page tables are freed.  Its extents go back to the pool tail,
    // owner cleared first, so no later free can match this state.
    if (!as || !g_ready.load(std::memory_order_acquire)) {
        return;
    }
    auto &slot = mmu::fork_arena_state(static_cast<mmu::address_space*>(as));
    auto *st = static_cast<as_arena*>(slot.exchange(nullptr, std::memory_order_acq_rel));
    if (!st) {
        return;
    }
    unsigned long n = 0;
    for (uint32_t e = st->chain; e != no_extent; n++) {
        uint32_t next = g_link[e];
        g_owner[e].store(nullptr, std::memory_order_release);
        pool_push_tail(e);
        e = next;
    }
    g_extents_in_use.fetch_sub(n, std::memory_order_relaxed);
    g_committed.fetch_sub(st->committed, std::memory_order_relaxed);
    g_live_states.fetch_sub(1, std::memory_order_relaxed);
    kernel_heap_scope kh;
    delete st;
}

arena_stats stats()
{
    arena_stats s;
    s.live_states = g_live_states.load(std::memory_order_relaxed);
    s.extents_in_use = g_extents_in_use.load(std::memory_order_relaxed);
    s.committed_bytes = g_committed.load(std::memory_order_relaxed);
    s.foreign_frees = g_foreign_frees.load(std::memory_order_relaxed);
    s.atomic_retained = g_atomic_retained.load(std::memory_order_relaxed);
    return s;
}

} // namespace fork_arena

// -----------------------------------------------------------------------------
// C-linkage accessors for the per-thread force_kernel_heap depth, exported to
// kernel modules (libsolaris.so / OpenZFS) via exported_symbols.  A module that
// allocates a kernel object which MUST be coherent across every fork address
// space -- e.g. a zio_t, whose embedded io_cv/io_lock a forked waiter blocks on
// while the AS0 block-completion thread signals it, and which that same AS0
// thread dereferences via bio->bio_caller1 in vdev_disk_bio_done -- brackets
// that allocation with fork_kernel_heap_push()/pop() so the object lands on the
// identity heap (shared verbatim in every AS) instead of the COW fork arena.
// Mirrors the C++ fork_arena::kernel_heap_scope used in-kernel.
extern "C" void fork_kernel_heap_push(void) { ++fork_arena::force_kernel_heap; }
extern "C" void fork_kernel_heap_pop(void)  { --fork_arena::force_kernel_heap; }

#else // !CONF_fork

// NON-FORK BUILD: the cross-address-space coherence scopings still CALL these
// from several BSD/libc sites, and the Makefile only links core/fork_arena.o
// when conf_fork=1, so a default (conf_fork=0) build would fail at LINK with
// "undefined reference to fork_kernel_heap_push".  Define the no-op once here.
// With no fork there is no COW arena, so "force allocations onto the identity
// heap" is already true unconditionally and the no-op is semantically correct.
extern "C" void fork_kernel_heap_push(void) { }
extern "C" void fork_kernel_heap_pop(void)  { }

#endif // CONF_fork
