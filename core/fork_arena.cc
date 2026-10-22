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
#include <cstring>
#include <cstdlib>
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
// All allocator state (free-list heads, bump cursors, lock) is in kernel BSS --
// never in arena pages -- so arena management never faults an arena page under
// a recycle lock. Returned-chunk writes may COW-fault.
//
// WHOLE-ARENA PER-AS WINDOWS (leak #2 cure).  Every live address space owns one
// disjoint VA window in a single 512 GiB application slot (slot 97).  An AS
// bump-carves inside its own window and recycles freed chunks through its own
// per-class free-list.  Because a VA identifies its owning AS by pure BSS
// arithmetic -- owner_slot(p) = (p - window_base) / window_sz -- a free() can
// decide same-AS vs foreign WITHOUT READING THE ARENA PAGE: a foreign free is
// an unconditional no-op, so a stale cross-AS free never reads a divergent COW
// header nor pushes a foreign base into a live AS's recycle cache.  The whole
// window is reclaimed wholesale by destroy_address_space's page-table teardown
// on reap.  There is NO shared global bump, so there is nothing recycled across
// address spaces and no cross-AS quiescence to prove.
//
// FORK_SHARED_BUMP (compile flag, default 0): restores the OLD shared slot-96
// global bump + unconditional slot-96 recover/push.  It reintroduces the
// cross-AS recycle hazard and is used only by the diagnostic C2 negative
// control (see the uncommitted fork_arena.c2probe.cc diagnostic variant).  It
// is never the shipped path.
// -----------------------------------------------------------------------------

#ifndef FORK_SHARED_BUMP
#define FORK_SHARED_BUMP 0
#endif

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

// Bounded recycle cache, not allocation ownership metadata. Dropping a free
// entry retains its mapping until AS teardown, but never reissues a live chunk.
constexpr uint16_t recycle_capacity = 1024;
struct recycle_entry {
    uintptr_t chunk;
    uint16_t next;
};

struct chunk_header {
    uint32_t class_idx;   // index into the size-class table (class_size_for)
    uint32_t magic;
};
constexpr uint32_t chunk_magic = 0x464b4152;   // "FKAR"

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

// --- all state below is kernel BSS, never in arena pages ---
//
// The per-AS free-lists are PER-ADDRESS-SPACE, NOT global.  This is
// load-bearing for fork correctness.  An arena chunk is COW-PRIVATE per child
// after fork: the same VA holds a different physical page in the parent and in
// each child.  A free-list whose HEAD lives in shared kernel BSS but whose
// `next` LINKS live inside those COW-private chunks is incoherent across the
// fork boundary: two address spaces sharing one head pop the same chunk and
// read its `next` from their OWN divergent copy, so one side recycles (and
// overwrites with fresh application data) a chunk the other still holds live.
// PostgreSQL hit this as a memory-context block list whose `next` had been
// clobbered with a path string ("...pid...") by another process, then
// SIGSEGV'd walking it in AllocSetReset.  Keying the free-list on the current
// address_space keeps each process's recycling inside its own COW domain: a
// chunk freed by one process is only ever re-handed to that same process, whose
// links stay self-coherent.
//
// Recycle descriptors and their links are identity-resident. No arena access,
// allocator call or fault is allowed while holding the recycle spinlock.
std::atomic<bool> g_ready{false};

// WHOLE-ARENA PER-AS WINDOWS (leak #2 cure).  Each live AS carves fresh chunks
// from its own COW-private disjoint window in a dedicated app slot (97), clear
// of the mmap hole.  Each AS maps its own window in its own page tables;
// destroy_address_space frees it wholesale on reap.  free() of a pointer in
// this slot that is NOT owned by the freeing AS is an unconditional NO-OP
// (owner arithmetic below), so a cross-AS free never reads the chunk header at
// a VA mapped only in the dead/other child.  win_slot_base/win_slot_end are
// defined in the header (contains() needs them too).
constexpr size_t    win_region_sz = 1ull << 20;       // 1 MiB per mapped region

// Per-AS DISJOINT VA windows.
//
// WHY THE WINDOWS (this is the whole fix).  A window VA identifies its owning
// AS: (va - win_slot_base) / win_window_sz IS the owning slot index.  So free()
// decides same-AS vs foreign WITHOUT READING THE ARENA PAGE -- a foreign free
// is an unconditional no-op (C2-safe), while the OWNING AS recycles through its
// per-class free-list (committed RAM tracks LIVE heap, not lifetime churn).
//
// 512 GiB (slot 97) / 256 AS slots = 2 GiB of VA per AS by default.  VA only;
// RAM is still just the pages actually mapped -- and now they get REUSED.  The
// window size is boot-tunable via --fork-arena-size-mb (see set_size_mb / init).
constexpr size_t    win_window_sz_default = (win_slot_end - win_slot_base) / 256;
static_assert(win_window_sz_default >= (1ull << 31), "default window too small");
// NOTE: region size is a RAM/overhead knob. Eager mmap_populate commits the
// WHOLE region up front, so a large region (e.g. 32 MiB) x many concurrent
// backends overcommits RAM and OOMs even though it is freed on reap. 1 MiB
// keeps the concurrent working set small (~1 MiB x live backends) while still
// amortizing the map_anon cost over ~64 avg chunks. Measured: 32 MiB OOMs at
// exhaustion; 256 KiB and 1 MiB keep mem_free FLAT (leak-free) with no OOM.

// Effective per-AS window size.  Set in init() from --fork-arena-size-mb (MiB),
// else win_window_sz_default.  Read this (g_window_sz), not the default, after
// init.  0 until init() runs, so owner arithmetic is never performed before
// ready() (alloc/free both bail on !g_ready).
uintptr_t g_window_sz = 0;

// Requested per-AS window size in BYTES, set by the loader from the
// --fork-arena-size-mb cmdline option BEFORE init() runs (0 = default).  Read
// from the kernel cmdline rather than getenv because init() runs before the
// application environment is populated.
std::atomic<size_t> g_requested_size{0};

#if FORK_SHARED_BUMP
// NEGATIVE CONTROL ONLY (-DFORK_SHARED_BUMP=1): the OLD shared slot-96 global
// bump.  Reintroduces the cross-AS recycle hazard.  Never the shipped path.
std::atomic<uintptr_t> g_bump{0};   // next never-yet-carved VA (GLOBAL: unique VA)
uintptr_t g_bump_end = 0;           // arena_base + arena_size
#endif

// OSV_FORK_ARENA_RECLAIM: 1 (default) => per-AS window arena (the fix);
//                         0            => old identity-heap fallback (the leak) A/B.
std::atomic<int> g_reclaim_on{-1};
inline bool reclaim_on()
{
    int v = g_reclaim_on.load(std::memory_order_relaxed);
    if (v < 0) {
        const char *e = getenv("OSV_FORK_ARENA_RECLAIM");
        v = (e && e[0] == '0') ? 0 : 1;
        g_reclaim_on.store(v, std::memory_order_relaxed);
    }
    return v != 0;
}

// BSS range check: is @p inside the per-AS window slot? Reads no arena page.
inline bool in_window(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= win_slot_base && a < win_slot_end;
}

// Which AS slot OWNS window pointer @p, by arithmetic alone.  Reads no arena
// page and takes no lock -- this is what makes the same-AS vs foreign decision
// in free() safe (see the win_window_sz note above).
inline unsigned win_owner_slot(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return (unsigned)((a - win_slot_base) / g_window_sz);
}

// FOOTPRINT PROBE: window bytes COMMITTED (mapped, eagerly populated -> real
// RAM), bytes RECYCLED through a same-AS free, and foreign frees no-op'd
// (C2-safe).  Window pages are committed by mmap_populate, NOT by a COW fault,
// so a COW-fault-only probe misses them.  Lightweight counters only.
std::atomic<unsigned long> g_committed{0};
std::atomic<unsigned long> g_recycled{0};
std::atomic<unsigned long> g_foreign_frees{0};

// Per-address-space free-list state.  Keyed by the opaque address_space* the
// current thread runs in (mmu::current_address_space()).  A small fixed table
// with linear probing: the concurrent-process count is modest (postmaster +
// its backends/aux), and if it ever fills, that address space simply runs
// bump-only (no recycling) -- correct, just less space-efficient.  Slot
// acquisition takes a brief spinlock; per-AS recycle operations use a separate
// short spinlock over identity metadata only.  A slot is reclaimed when its
// address space is destroyed (release_as(), from mmu::destroy_address_space).
struct as_freelist {
    std::atomic<void*>      owner{nullptr};   // address_space* key, null == free slot
    spinlock recycle_lock;
    uint16_t heads[num_classes];
    uint16_t unused;
    recycle_entry entries[recycle_capacity];

    void reset_recycle()
    {
        for (auto& head : heads) head = recycle_capacity;
        for (uint16_t i = 0; i < recycle_capacity; ++i) entries[i].next = i + 1;
        unused = 0;
    }

    void *pop(unsigned idx)
    {
        SCOPE_LOCK(recycle_lock);
        auto n = heads[idx];
        if (n == recycle_capacity) return nullptr;
        auto chunk = entries[n].chunk;
        heads[idx] = entries[n].next;
        entries[n].next = unused;
        unused = n;
        return reinterpret_cast<void*>(chunk);
    }

    bool push(uintptr_t chunk, unsigned idx)
    {
        SCOPE_LOCK(recycle_lock);
        // ponytail: 1024 cached frees/AS (~4 MiB BSS overall). Full cache
        // drops recycling; grow metadata only if retained-mapping churn matters.
        if (unused == recycle_capacity) return false;
        auto n = unused;
        unused = entries[n].next;
        entries[n].chunk = chunk;
        entries[n].next = heads[idx];
        heads[idx] = n;
        return true;
    }
    // WHOLE-ARENA PER-AS WINDOW bump cursors (COW-private, slot 97).  Freed
    // wholesale by destroy_address_space page teardown on reap; reset in
    // release_as so a recycled slot never inherits a dead child's window VA.
    std::atomic<uintptr_t>  win_next{0};      // next byte to carve in current region
    std::atomic<uintptr_t>  win_end{0};       // end of current region (0 = none yet)
    mutex                   win_lock;         // serializes growth, never the fast carve
    // Bytes of this AS's window mapped so far.  Doubles as the map cursor
    // (windows fill monotonically) and the high-water that free() range-checks
    // a candidate pointer against -- both reads are BSS-only, never an arena
    // page.
    std::atomic<uintptr_t>  win_mapped{0};
};
constexpr unsigned max_as_slots = 256;
as_freelist g_as_freelists[max_as_slots];
spinlock g_slot_lock;   // guards slot acquisition only (rare: once per AS)

// Find (or create) the free-list slot for address space @as.  Returns nullptr
// only if the table is full (caller then runs bump-only / identity fallback).
as_freelist *slot_for(void *as)
{
    // nullptr is the free-slot sentinel, never an address-space identity.
    if (!as) {
        return nullptr;
    }
    // note: O(max_as_slots) linear scan per alloc/free; owners pack from
    // index 0 so the common case is a short scan. Swap for a hash keyed on
    // (address_space*) if the process count ever makes this measurable.
    // Fast path: already-claimed slot, no lock.
    for (unsigned i = 0; i < max_as_slots; i++) {
        if (g_as_freelists[i].owner.load(std::memory_order_acquire) == as) {
            return &g_as_freelists[i];
        }
    }
    // Slow path: claim a free slot under the spinlock.  Re-scan under the lock
    // (another thread may have just claimed one for the same AS).
    SCOPE_LOCK(g_slot_lock);
    for (unsigned i = 0; i < max_as_slots; i++) {
        if (g_as_freelists[i].owner.load(std::memory_order_relaxed) == as) {
            return &g_as_freelists[i];
        }
    }
    for (unsigned i = 0; i < max_as_slots; i++) {
        if (!g_as_freelists[i].owner.load(std::memory_order_relaxed)) {
            g_as_freelists[i].reset_recycle();
            // A freshly-claimed slot starts with no window (never inherit a
            // prior owner's VA).
            g_as_freelists[i].win_next.store(0, std::memory_order_relaxed);
            g_as_freelists[i].win_end.store(0, std::memory_order_relaxed);
            g_as_freelists[i].win_mapped.store(0, std::memory_order_relaxed);
            // g_slot_lock serializes writers; publish only AFTER initialization.
            g_as_freelists[i].owner.store(as, std::memory_order_release);
            return &g_as_freelists[i];
        }
    }
    return nullptr;   // table full: bump-only for this AS
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

void set_size_mb(unsigned long mb)
{
    if (mb) {
        g_requested_size.store((size_t)mb << 20, std::memory_order_relaxed);
    }
}

void init()
{
    if (g_ready.load(std::memory_order_acquire)) {
        return;
    }
    // Effective per-AS window size: --fork-arena-size-mb (MiB) overrides the
    // default, clamped to [512 MiB .. the 2 GiB slot share].  NOTE the SEMANTIC
    // CHANGE from the shared-arena era: this no longer sizes a shared, eagerly
    // committed boot-RAM arena (there is none).  It sizes each AS's PRIVATE VA
    // WINDOW; RAM is still only the pages an AS actually maps (lazily, in
    // win_region_sz increments) and is freed wholesale on reap.  So growing it
    // raises the per-backend VA budget before an AS falls back to the identity
    // heap; it does NOT cost committed boot RAM (the shared-arena GROW's one
    // drawback is gone).
    size_t eff_window = win_window_sz_default;
    size_t want = g_requested_size.load(std::memory_order_relaxed);
    if (want) {
        if (want < (512ull << 20)) want = 512ull << 20;             // floor 512 MiB
        if (want > win_window_sz_default) want = win_window_sz_default; // cap at the slot share
        eff_window = want;
    }
    g_window_sz = eff_window;

#if FORK_SHARED_BUMP
    // NEGATIVE CONTROL: pre-map and bump from a shared slot-96 arena.
    void *v = mmu::map_anon(reinterpret_cast<void*>(arena_base), arena_size,
                            mmu::mmap_fixed | mmu::mmap_populate, mmu::perm_rw);
    if (reinterpret_cast<uintptr_t>(v) != arena_base) {
        debugf("fork_arena: failed to reserve arena at %p (got %p)\n",
               reinterpret_cast<void*>(arena_base), v);
        return;
    }
    g_bump.store(arena_base, std::memory_order_relaxed);
    g_bump_end = arena_base + arena_size;
#endif
    // THE CURE: no shared arena to pre-map.  Each AS lazily maps its own window
    // in win_region_sz increments on first carve.  This also removes the fixed
    // boot-RAM commit the shared arena required.
    g_ready.store(true, std::memory_order_release);
}

// FOOTPRINT PROBE: count of per-AS free-list slots in use (== live processes
// that have allocated from the arena).
unsigned live_as_slots()
{
    unsigned n = 0;
    for (unsigned i = 0; i < max_as_slots; i++) {
        if (g_as_freelists[i].owner.load(std::memory_order_relaxed)) n++;
    }
    return n;
}

// FOOTPRINT PROBE: the window accounting the COW-fault probe cannot see.
// Window regions are committed by mmap_populate (eager, real RAM at map time),
// NOT by a COW write fault, so a COW-fault-only probe misses them ENTIRELY.
//   committed     = bytes of window region ever mapped (real RAM)
//   recycled      = bytes handed back through a same-AS free (the fix working)
//   foreign_frees = frees skipped because another AS owns the VA (C2-safe no-op)
void overflow_stats(unsigned long *committed, unsigned long *recycled,
                    unsigned long *foreign_frees, unsigned long *live_mapped)
{
    if (committed) *committed = g_committed.load(std::memory_order_relaxed);
    if (recycled) *recycled = g_recycled.load(std::memory_order_relaxed);
    if (foreign_frees) {
        *foreign_frees = g_foreign_frees.load(std::memory_order_relaxed);
    }
    if (live_mapped) {
        unsigned long t = 0;
        for (unsigned i = 0; i < max_as_slots; i++) {
            if (g_as_freelists[i].owner.load(std::memory_order_relaxed)) {
                t += g_as_freelists[i].win_mapped.load(std::memory_order_relaxed);
            }
        }
        *live_mapped = t;
    }
}

// FOOTPRINT PROBE: this AS's window bytes mapped (per-backend attribution).
unsigned long overflow_mapped_for(void *as)
{
    for (unsigned i = 0; i < max_as_slots; i++) {
        if (g_as_freelists[i].owner.load(std::memory_order_relaxed) == as) {
            return g_as_freelists[i].win_mapped.load(std::memory_order_relaxed);
        }
    }
    return 0;
}

bool ready()
{
    return g_ready.load(std::memory_order_acquire);
}

// Read end before next: observing a newly published end also observes its
// initial next. next is strictly increasing across regions, including when a
// large request abandons a small tail. Thus a stale CAS cannot succeed after
// growth, and an old end cannot authorize a carve in a newer region.
static void *window_carve(as_freelist *fl, size_t class_size)
{
    for (;;) {
        uintptr_t end = fl->win_end.load(std::memory_order_acquire);
        uintptr_t c = fl->win_next.load(std::memory_order_relaxed);
        if (!end || c > end || class_size > end - c) {
            return nullptr;
        }
        if (fl->win_next.compare_exchange_weak(c, c + class_size,
                std::memory_order_acq_rel, std::memory_order_relaxed)) {
            return reinterpret_cast<void*>(c);
        }
    }
}

static void *window_alloc(as_freelist *fl, size_t class_size)
{
    if (!sched::preemptable() || !arch::irq_enabled()) {
        return nullptr;
    }
    if (void *chunk = window_carve(fl, class_size)) {
        return chunk;
    }
    // Cover mutex waiter allocation as well as map_anon's nested allocations.
    // Neither may recurse into this arena; waiters must be AS-coherent too.
    kernel_heap_scope kh;
    SCOPE_LOCK(fl->win_lock);
    if (void *chunk = window_carve(fl, class_size)) {
        return chunk;
    }

    const unsigned slot = unsigned(fl - &g_as_freelists[0]);
    const uintptr_t win_base = win_slot_base + uintptr_t(slot) * g_window_sz;
    size_t rsz = align_up(class_size, win_region_sz);
    uintptr_t mapped = fl->win_mapped.load(std::memory_order_relaxed);
    if (rsz > g_window_sz - mapped) {
        return nullptr;
    }
    uintptr_t want = win_base + mapped;
    // Serialize selection, mapping AND publication. mmap_fixed replaces any
    // old mapping at want; racing maps cannot safely be discarded afterwards.
    void *v = mmu::map_anon(reinterpret_cast<void*>(want), rsz,
                            mmu::mmap_fixed | mmu::mmap_populate, mmu::perm_rw);
    if (reinterpret_cast<uintptr_t>(v) != want) {
        if (v) {
            mmu::munmap(v, rsz);
        }
        return nullptr; // unchanged cursor, so a failed mapping can be retried
    }
    fl->win_next.store(want + class_size, std::memory_order_relaxed);
    // Publish the free() range check before enabling fast carves via end.
    fl->win_mapped.store(mapped + rsz, std::memory_order_release);
    fl->win_end.store(want + rsz, std::memory_order_release);
    g_committed.fetch_add(rsz, std::memory_order_relaxed);
    return v;
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
    if (alignment < 16) {
        alignment = 16;
    }
    if (!size) {
        size = 1;   // malloc(0) still gets a distinct pointer with usable bytes
    }
    // Worst-case footprint: header + alignment padding + user bytes.  The
    // returned pointer is header_size past the chunk start when alignment
    // divides header_size; otherwise we align up within the chunk.
    size_t need = header_size + size + (alignment > header_size ? alignment : 0);
    if (need > max_alloc) {
        return nullptr;   // too big for the arena; caller uses the normal heap
    }
    unsigned idx = class_for(need);
    if (idx >= num_classes) {
        return nullptr;
    }
    size_t class_size = class_size_for(idx);

    // Per-address-space free-list: recycle only within this process's own COW
    // domain (see the note on as_freelist).  If the slot table is full, this AS
    // falls back to the identity heap.
    as_freelist *fl = slot_for(mmu::current_address_space());
    if (!fl) {
        return nullptr;   // slot table full: caller uses the identity heap
    }

    void *chunk = fl->pop(idx);
    if (!chunk) {
#if FORK_SHARED_BUMP
        // NEGATIVE CONTROL: carve off the shared global bump (the hazard).
        uintptr_t c = g_bump.fetch_add(class_size, std::memory_order_relaxed);
        if (c + class_size <= g_bump_end) {
            chunk = reinterpret_cast<void*>(c);
        } else if (reclaim_on()) {
            chunk = window_alloc(fl, class_size);
            if (!chunk) return nullptr;
        } else {
            return nullptr;
        }
#else
        // THE CURE: always carve from THIS AS's private window (slot 97),
        // reclaimed wholesale on reap.  No shared bump exists.
        if (reclaim_on()) {
            chunk = window_alloc(fl, class_size);
            if (!chunk) {
                return nullptr;   // window exhausted/unmappable: identity fallback
            }
        } else {
            return nullptr;   // reclaim off (A/B before arm): identity fallback (leak)
        }
#endif
    }

    // Now touch the chunk (a fresh chunk faults its page in here, with
    // preemption on).  The chunk is uniquely ours, so these writes race with
    // nobody.
    //
    // Place the header at the chunk start, return an aligned pointer after it.
    uintptr_t base = reinterpret_cast<uintptr_t>(chunk);
    uintptr_t user = align_up(base + header_size, alignment);
    auto *h = reinterpret_cast<chunk_header*>(user - sizeof(chunk_header));
    h->class_idx = idx;
    h->magic = chunk_magic;
    // Store the chunk base so free() can reconstruct it regardless of the
    // alignment padding.  header_size (16) == sizeof(chunk_header)(8) +
    // sizeof(uintptr_t)(8), and user >= base + header_size, so [base .. user)
    // is ours.
    *reinterpret_cast<uintptr_t*>(user - 16) = base;
    return reinterpret_cast<void*>(user);
}

namespace {
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

void free(void *p)
{
    // No header access in atomic context: population does not imply that an
    // inherited mapping is safely accessible here. Retain until AS teardown.
    if (!sched::preemptable() || !arch::irq_enabled()) {
        return;
    }
    if (!g_ready.load(std::memory_order_acquire)) {
        return;
    }
#if FORK_SHARED_BUMP
    // NEGATIVE CONTROL: the OLD shared slot-96 recover/push path -- the exact
    // cross-AS recycle hazard the cure retires.
    if (!in_window(p)) {
        uintptr_t base;
        unsigned idx;
        recover(p, base, idx);   // may be a divergent COW copy (the hazard)
        as_freelist *fl = slot_for(mmu::current_address_space());
        if (!fl) {
            return;
        }
        fl->push(base, idx);
        return;
    }
#endif
    // THE CURE: the window owner-arithmetic path handles EVERY arena pointer.
    //
    // The C2-safety constraint is absolute: a FOREIGN-AS free must not read the
    // chunk header, because that VA may be mapped only in the dead or other
    // child (cross-AS reaper / RCU quiescent / sibling) -> fault, or masked to a
    // divergent COW copy.  The OWNING AS can read an intact resident header.
    //
    // Per-AS disjoint windows make the owner test pure BSS arithmetic:
    // win_owner_slot(p) is the owning slot, and the current AS's slot is known
    // from slot_for().  Same slot -> ours, recycle.  Different slot -> foreign,
    // no-op (the window is reclaimed wholesale when the owning AS is reaped).
    // No arena page is touched on the foreign path.
    if (!in_window(p)) {
        return;   // not an arena pointer (cannot happen via contains(); defensive)
    }
    as_freelist *fl = slot_for(mmu::current_address_space());
    if (!fl) {
        return;             // no slot for this AS: cannot own it
    }
    unsigned my_slot = (unsigned)(fl - &g_as_freelists[0]);
    if (win_owner_slot(p) != my_slot) {
        // FOREIGN AS: no-op, and critically NO header read (C2-safe).  The
        // window is reclaimed wholesale when the owning AS is reaped.
        g_foreign_frees.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    // Ours.  Guard against a pointer inside our window but past what we ever
    // mapped (never produced by window_alloc; a BSS-only check, no read).
    uintptr_t win_base = win_slot_base + (uintptr_t)my_slot * g_window_sz;
    if (reinterpret_cast<uintptr_t>(p) - win_base >=
            fl->win_mapped.load(std::memory_order_acquire)) {
        return;
    }
    uintptr_t obase;
    unsigned oidx;
    recover(p, obase, oidx);  // requires an intact resident same-AS allocation
    if (fl->push(obase, oidx)) {
        g_recycled.fetch_add(class_size_for(oidx), std::memory_order_relaxed);
    }
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
    // Called from mmu::destroy_address_space when a fork child's address space
    // is torn down.  Drop the child's free-list slot so it can be reused by a
    // later process, and drop its recycled chunks (their VA is COW-private to
    // the dying AS and its physical pages are freed with the page tables, so
    // nothing here dereferences the child's now-gone memory -- we only clear
    // the shared BSS slot).  The whole per-AS window is unmapped by the
    // page-table teardown just before this runs.
    if (!as || !g_ready.load(std::memory_order_acquire)) {
        return;
    }
    // The caller must have quiesced ALL users of this AS, including growth
    // mutex waiters. Its page tables have already been destroyed. The writer
    // lock prevents a new owner from claiming this slot until reset completes.
    SCOPE_LOCK(g_slot_lock);
    for (unsigned i = 0; i < max_as_slots; i++) {
        if (g_as_freelists[i].owner.load(std::memory_order_relaxed) == as) {
            g_as_freelists[i].reset_recycle();
            // Reset the per-AS window bump. The dead child's window (slot 97)
            // was just unmapped by the page-table teardown; a slot recycled to
            // a NEW AS must NOT inherit the dead VA.
            g_as_freelists[i].win_next.store(0, std::memory_order_relaxed);
            g_as_freelists[i].win_end.store(0, std::memory_order_relaxed);
            g_as_freelists[i].win_mapped.store(0, std::memory_order_relaxed);
            g_as_freelists[i].owner.store(nullptr, std::memory_order_release);
            return;
        }
    }
}

void leak_probe_stats(unsigned long *bump_used, unsigned long *as_slots)
{
    if (bump_used) {
        // Sum of all live AS's mapped window bytes (the high-water of committed
        // window space; there is no shared bump under the cure).
        unsigned long t = 0;
        if (g_ready.load(std::memory_order_acquire)) {
            for (unsigned i = 0; i < max_as_slots; i++) {
                if (g_as_freelists[i].owner.load(std::memory_order_relaxed)) {
                    t += g_as_freelists[i].win_mapped.load(std::memory_order_relaxed);
                }
            }
        }
        *bump_used = t;
    }
    if (as_slots) {
        unsigned long n = 0;
        for (unsigned i = 0; i < max_as_slots; i++) {
            if (g_as_freelists[i].owner.load(std::memory_order_relaxed)) n++;
        }
        *as_slots = n;
    }
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
