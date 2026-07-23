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
#include <osv/mutex.h>
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
mutex g_lock;
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
    SCOPE_LOCK(g_lock);
    if (g_ready.load(std::memory_order_relaxed)) {
        return;
    }
    // Reserve the arena VA as a fixed anonymous app-slot mapping.  Pages fault
    // in lazily (anon vma fault path, irqs on) on first touch; clone_address_
    // space() COW-clones the whole vma per child.  Not populated eagerly: 4 GiB
    // of VA costs no physical memory until touched.
    void *v = mmu::map_anon(reinterpret_cast<void*>(arena_base), arena_size,
                            mmu::mmap_fixed, mmu::perm_rw);
    if (reinterpret_cast<uintptr_t>(v) != arena_base) {
        // Could not pin the arena at its fixed VA: leave routing off (falls
        // back to the normal identity heap; fork isolation just won't apply).
        debug("fork_arena: failed to reserve arena at %p (got %p)\n",
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
    WITH_LOCK(g_lock) {
        if (g_freelist[idx]) {
            free_node *n = g_freelist[idx];
            g_freelist[idx] = n->next;
            chunk = n;
        } else {
            // Carve a fresh class_size chunk off the bump pointer.
            uintptr_t c = g_bump;
            if (c + class_size > g_end) {
                return nullptr;   // arena exhausted
            }
            g_bump = c + class_size;
            chunk = reinterpret_cast<void*>(c);
        }
    }

    // Place the header at the chunk start, return an aligned pointer after it.
    uintptr_t base = reinterpret_cast<uintptr_t>(chunk);
    uintptr_t user = align_up(base + header_size, alignment);
    // Header lives immediately before `user` so free() can find it from the
    // user pointer alone.
    auto *h = reinterpret_cast<chunk_header*>(user - sizeof(chunk_header));
    // Stash the chunk base so free() can reconstruct it regardless of the
    // alignment padding we applied.
    // We keep it simple: encode class_shift + magic; the chunk base is
    // recoverable by rounding the user pointer down to class_size (chunks are
    // class_size-aligned only when class_size <= alignment of the bump start).
    // To be robust for any alignment, store the base offset too.
    h->class_shift = s;
    h->magic = chunk_magic;
    // Store the chunk base in the 16 bytes preceding the header word pair.
    // header_size (32) >= sizeof(chunk_header)(8) + sizeof(uintptr_t)(8), and
    // user - header_size == base, so [base .. user) is ours to use.
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
    recover(p, base, s);
    unsigned idx = s - min_class_shift;
    auto *n = reinterpret_cast<free_node*>(base);
    WITH_LOCK(g_lock) {
        n->next = g_freelist[idx];
        g_freelist[idx] = n;
    }
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
