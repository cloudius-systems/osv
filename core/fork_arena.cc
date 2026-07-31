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
#include <atomic>
#include <cstring>

// -----------------------------------------------------------------------------
// Fork heap arena implementation.  See include/osv/fork_arena.hh for the why.
//
// Layout of a served chunk:
//     [ chunk_header (8 bytes) ][ user data ... ]
//                               ^ returned pointer (aligned)
// The header records the size class so free()/usable_size() need no external
// bookkeeping.  All allocator state (free-list heads, bump pointer, lock) is in
// kernel BSS -- never in arena pages -- so arena management never faults an
// arena page and never recurses into malloc during fork's page-table work.
// -----------------------------------------------------------------------------

namespace fork_arena {

volatile __thread unsigned force_kernel_heap = 0;

namespace {

// Size classes: 32, 64, ... up to max_alloc, plus alignment slack.  A request
// picks the smallest class that fits (header + user + alignment padding).
constexpr size_t min_class_shift = 5;                 // 32 bytes
constexpr size_t max_class_shift = 21;                // 2 MiB (== max_alloc)
constexpr unsigned num_classes = max_class_shift - min_class_shift + 1;

struct free_node {
    free_node *next;
};

struct chunk_header {
    uint32_t class_shift;   // size class = 1 << class_shift
    uint32_t magic;
};
constexpr uint32_t chunk_magic = 0x464b4152;   // "FKAR"
constexpr size_t header_size = 32;             // >= sizeof(chunk_header), keeps 16/32-align

// --- all state below is kernel BSS, never in arena pages ---
//
// The bump pointer is a single GLOBAL atomic: it carves each fresh chunk a VA
// that is unique across EVERY address space, so a bump-allocated chunk is never
// double-owned.  That is correct to share.
//
// The free-lists are PER-ADDRESS-SPACE, NOT global.  This is load-bearing for
// fork correctness.  An arena chunk is COW-PRIVATE per child after fork: the
// same VA holds a different physical page in the parent and in each child.  A
// free-list whose HEAD lives in shared kernel BSS but whose `next` LINKS live
// inside those COW-private chunks is incoherent across the fork boundary: two
// address spaces sharing one head pop the same chunk and read its `next` from
// their OWN divergent copy, so one side recycles (and overwrites with fresh
// application data) a chunk the other still holds live.  PostgreSQL hit this
// as a memory-context block list whose `next` had been clobbered with a path
// string ("...pid...") by another process, then SIGSEGV'd walking it in
// AllocSetReset.  Keying the free-list on the current address_space keeps each
// process's recycling inside its own COW domain: a chunk freed by one process
// is only ever re-handed to that same process, whose links stay self-coherent.
//
// Each per-AS free-list head is still a lock-free Treiber stack, so the COW
// page fault that writing a chunk's link may trigger is serviced with no lock
// held and preemption on (the original correctness property is preserved).
std::atomic<bool> g_ready{false};
std::atomic<uintptr_t> g_bump{0};   // next never-yet-carved VA (GLOBAL: unique VA)
uintptr_t g_end = 0;                // arena_base + arena_size

// Per-address-space free-list state.  Keyed by the opaque address_space* the
// current thread runs in (mmu::current_address_space()).  A small fixed table
// with linear probing: the concurrent-process count is modest (postmaster +
// its backends/aux), and if it ever fills, that address space simply runs
// bump-only (no recycling) -- correct, just less space-efficient.  Slot
// acquisition takes a brief spinlock; the per-AS freelist ops themselves stay
// lock-free.  A slot is reclaimed when its address space is destroyed
// (release_as(), called from mmu::destroy_address_space).
struct as_freelist {
    std::atomic<void*>      owner{nullptr};   // address_space* key, null == free slot
    std::atomic<free_node*> heads[num_classes];
};
constexpr unsigned max_as_slots = 256;
as_freelist g_as_freelists[max_as_slots];
spinlock g_slot_lock;   // guards slot acquisition only (rare: once per AS)

// Find (or create) the free-list slot for address space @as.  Returns nullptr
// only if the table is full (caller then runs bump-only).
as_freelist *slot_for(void *as)
{
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
        void *expect = nullptr;
        if (g_as_freelists[i].owner.compare_exchange_strong(
                expect, as, std::memory_order_acq_rel)) {
            for (unsigned c = 0; c < num_classes; c++) {
                g_as_freelists[i].heads[c].store(nullptr, std::memory_order_relaxed);
            }
            return &g_as_freelists[i];
        }
    }
    return nullptr;   // table full: bump-only for this AS
}

unsigned class_for(size_t total)
{
    // total includes header + user + alignment slack; round up to a power of 2
    // >= 1<<min_class_shift.
    unsigned s = min_class_shift;
    while ((size_t(1) << s) < total) {
        s++;
    }
    return s;
}

} // anonymous namespace

void init()
{
    if (g_ready.load(std::memory_order_acquire)) {
        return;
    }
    // Reserve the arena VA as a fixed anonymous app-slot mapping, EAGERLY
    // POPULATED (mmap_populate): every arena page is backed with real RAM at
    // init, so fork_arena::alloc() NEVER demand-faults on a bump-carved page.
    // That is load-bearing for correctness, not just latency: malloc ->
    // fork_arena::alloc can be entered from an IRQs-off / preemption-off
    // context (e.g. under concurrent PG load, mid-exception), where a demand
    // fault would trip page_fault's assert(preemptable && irq_if) and abort.
    // With the whole 512 MiB pre-faulted, alloc's first write hits an already-
    // present page and cannot fault -- safe from any context.
    // clone_address_space() still COW-clones the whole vma per child; the child
    // only faults on WRITE (COW break), which happens from app context with
    // irqs/preemption on, so that path keeps the original invariant.
    void *v = mmu::map_anon(reinterpret_cast<void*>(arena_base), arena_size,
                            mmu::mmap_fixed | mmu::mmap_populate, mmu::perm_rw);
    if (reinterpret_cast<uintptr_t>(v) != arena_base) {
        // Could not pin the arena at its fixed VA: leave routing off (falls
        // back to the normal identity heap; fork isolation just won't apply).
        debugf("fork_arena: failed to reserve arena at %p (got %p)\n",
               reinterpret_cast<void*>(arena_base), v);
        return;
    }
    g_bump.store(arena_base, std::memory_order_relaxed);
    g_end = arena_base + arena_size;
    g_ready.store(true, std::memory_order_release);
}

bool ready()
{
    return g_ready.load(std::memory_order_acquire);
}

void *alloc(size_t size, size_t alignment)
{
    if (!g_ready.load(std::memory_order_acquire)) {
        return nullptr;
    }
    if (alignment < 16) {
        alignment = 16;
    }
    // Worst-case footprint: header + alignment padding + user bytes.  The
    // returned pointer is header_size past the chunk start when alignment
    // divides header_size; otherwise we align up within the chunk.
    size_t need = header_size + size + (alignment > header_size ? alignment : 0);
    if (need > max_alloc) {
        return nullptr;   // too big for the arena; caller uses the normal heap
    }
    unsigned s = class_for(need);
    if (s > max_class_shift) {
        return nullptr;
    }
    unsigned idx = s - min_class_shift;
    size_t class_size = size_t(1) << s;

    // Per-address-space free-list: recycle only within this process's own COW
    // domain (see the note on as_freelist).  If the slot table is full, this AS
    // runs bump-only.
    as_freelist *fl = slot_for(mmu::current_address_space());

    void *chunk = nullptr;
    // Lock-free pop from THIS AS's size-class Treiber stack.  Reading
    // head->next touches an arena page (a previously-freed chunk of THIS AS);
    // that page is already faulted in and COW-private to this AS, we hold no
    // lock, so a COW read is fine.  Preemption stays on: no illegal fault.
    if (fl) {
        free_node *head = fl->heads[idx].load(std::memory_order_acquire);
        while (head) {
            free_node *next = head->next;
            if (fl->heads[idx].compare_exchange_weak(head, next,
                    std::memory_order_acq_rel, std::memory_order_acquire)) {
                chunk = head;
                break;
            }
        }
    }
    if (!chunk) {
        // Carve a fresh class_size chunk off the bump pointer (atomic).
        uintptr_t c = g_bump.fetch_add(class_size, std::memory_order_relaxed);
        if (c + class_size > g_end) {
            return nullptr;   // arena exhausted
        }
        chunk = reinterpret_cast<void*>(c);
    }

    // Now touch the chunk (a fresh bump chunk faults its page in here, with
    // preemption on).  The chunk is uniquely ours, so these writes race with
    // nobody.
    //
    // Place the header at the chunk start, return an aligned pointer after it.
    uintptr_t base = reinterpret_cast<uintptr_t>(chunk);
    uintptr_t user = align_up(base + header_size, alignment);
    auto *h = reinterpret_cast<chunk_header*>(user - sizeof(chunk_header));
    h->class_shift = s;
    h->magic = chunk_magic;
    // Store the chunk base so free() can reconstruct it regardless of the
    // alignment padding.  header_size (32) >= sizeof(chunk_header)(8) +
    // sizeof(uintptr_t)(8), and user - header_size == base, so [base .. user)
    // is ours.
    *reinterpret_cast<uintptr_t*>(user - 16) = base;
    return reinterpret_cast<void*>(user);
}

namespace {
inline void recover(void *p, uintptr_t &base, unsigned &s)
{
    uintptr_t user = reinterpret_cast<uintptr_t>(p);
    auto *h = reinterpret_cast<chunk_header*>(user - sizeof(chunk_header));
    assert(h->magic == chunk_magic);
    s = h->class_shift;
    base = *reinterpret_cast<uintptr_t*>(user - 16);
}
} // anonymous namespace

void free(void *p)
{
    uintptr_t base;
    unsigned s;
    recover(p, base, s);   // reads header (populated page), no lock, no fault
    unsigned idx = s - min_class_shift;
    auto *n = reinterpret_cast<free_node*>(base);
    // Push onto THIS address space's free-list.  If the slot table is full,
    // drop the chunk (it leaks arena VA but is never mis-recycled across the
    // COW boundary -- correctness over the space of a full table).
    as_freelist *fl = slot_for(mmu::current_address_space());
    if (!fl) {
        return;
    }
    // Lock-free Treiber push.  Writing n->next touches the chunk page, which
    // after fork is copy-on-write and COW-private to THIS AS: with no lock held
    // and preemption on, the resulting COW page fault is legal (OSv forbids
    // faulting non-preemptable).
    free_node *head = fl->heads[idx].load(std::memory_order_relaxed);
    do {
        n->next = head;
    } while (!fl->heads[idx].compare_exchange_weak(head, n,
                 std::memory_order_release, std::memory_order_relaxed));
}

size_t usable_size(void *p)
{
    uintptr_t base;
    unsigned s;
    recover(p, base, s);
    size_t class_size = size_t(1) << s;
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
    // the shared BSS slot).  No teardown needed for the shared bump pointer.
    if (!g_ready.load(std::memory_order_acquire)) {
        return;
    }
    SCOPE_LOCK(g_slot_lock);
    for (unsigned i = 0; i < max_as_slots; i++) {
        if (g_as_freelists[i].owner.load(std::memory_order_relaxed) == as) {
            for (unsigned c = 0; c < num_classes; c++) {
                g_as_freelists[i].heads[c].store(nullptr, std::memory_order_relaxed);
            }
            g_as_freelists[i].owner.store(nullptr, std::memory_order_release);
            return;
        }
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

#endif // CONF_fork
