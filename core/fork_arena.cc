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
#include <osv/spinlock.h>
#include <osv/align.hh>
#include <osv/debug.hh>
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

__thread unsigned force_kernel_heap = 0;

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
// A spinlock (disables preemption) guards ONLY the free-list heads and the
// bump pointer -- all in kernel BSS, so no page fault ever happens while it is
// held.  The header write that touches a (possibly not-yet-faulted) arena page
// is done AFTER releasing the lock, when preemption is on again, so a fresh-
// page fault there is legal.  (A sleeping mutex would be wrong: std_malloc can
// be called with preemption disabled, and a mutex would try to context-switch
// there -- the original arena crash.)
spinlock_t g_lock;
std::atomic<bool> g_ready{false};
uintptr_t g_bump = 0;         // next never-yet-carved VA
uintptr_t g_end = 0;          // arena_base + arena_size
free_node *g_freelist[num_classes] = {};

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
    // Reserve the arena VA as a fixed anonymous app-slot mapping.  Pages fault
    // in lazily (anon vma fault path, irqs on) on first touch; clone_address_
    // space() COW-clones the whole vma per child (splitting any 2 MB pages to
    // 4 K as it goes).  Not populated eagerly: VA costs no RAM until touched.
    void *v = mmu::map_anon(reinterpret_cast<void*>(arena_base), arena_size,
                            mmu::mmap_fixed, mmu::perm_rw);
    if (reinterpret_cast<uintptr_t>(v) != arena_base) {
        // Could not pin the arena at its fixed VA: leave routing off (falls
        // back to the normal identity heap; fork isolation just won't apply).
        debugf("fork_arena: failed to reserve arena at %p (got %p)\n",
               reinterpret_cast<void*>(arena_base), v);
        return;
    }
    g_bump = arena_base;
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

    void *chunk = nullptr;
    // Under the spinlock: touch ONLY BSS metadata (free-list, bump).  No arena
    // page is written here, so preemption-disabled == no illegal fault.
    spin_lock(&g_lock);
    if (g_freelist[idx]) {
        free_node *n = g_freelist[idx];
        g_freelist[idx] = n->next;
        chunk = n;
    } else {
        uintptr_t c = g_bump;
        if (c + class_size > g_end) {
            spin_unlock(&g_lock);
            return nullptr;   // arena exhausted
        }
        g_bump = c + class_size;
        chunk = reinterpret_cast<void*>(c);
    }
    spin_unlock(&g_lock);

    // Lock released: now safe to touch the chunk (a fresh bump chunk faults its
    // page in here, with preemption on).  The chunk is uniquely ours, so these
    // writes race with nobody.  Reading g_freelist[idx]->next above touched an
    // already-populated page (the node was a live allocation once), also safe.
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
    // Pre-fault the chunk's first word BEFORE disabling preemption: after fork
    // this chunk may be a copy-on-write page, and the freelist-link write below
    // must not trigger a COW page fault while the arena spinlock holds
    // preemption off (OSv forbids faulting non-preemptable -- assert in
    // page_fault).  Touching it here, preemption on, resolves the COW copy
    // first; the spinlocked write then hits a private writable page.
    n->next = nullptr;
    spin_lock(&g_lock);
    n->next = g_freelist[idx];
    g_freelist[idx] = n;
    spin_unlock(&g_lock);
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

} // namespace fork_arena

#endif // CONF_fork
