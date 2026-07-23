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
// Lock-free (no preemption disabled, no sleeping lock): the arena's freelist
// heads are Treiber stacks and the bump pointer is an atomic.  This is the
// crucial correctness property for fork: an arena chunk may be a copy-on-write
// page after fork, so writing its freelist link (in free()) can trigger a COW
// page fault -- which OSv only permits when preemption AND interrupts are
// enabled.  A spinlock (preemption off) or a caller that already holds
// preempt_lock would make that fault illegal (assert in page_fault).  With no
// lock held here, the COW fault is always serviced legally.
std::atomic<free_node*> g_freelist[num_classes];
std::atomic<bool> g_ready{false};
std::atomic<uintptr_t> g_bump{0};   // next never-yet-carved VA
uintptr_t g_end = 0;                // arena_base + arena_size

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

    void *chunk = nullptr;
    // Lock-free pop from the size class's Treiber stack.  Reading head->next
    // touches an arena page (a previously-freed chunk); that page is already
    // faulted in (it was allocated once), and we hold no lock, so a COW read is
    // fine.  Preemption stays on throughout: no illegal fault window.
    free_node *head = g_freelist[idx].load(std::memory_order_acquire);
    while (head) {
        free_node *next = head->next;
        if (g_freelist[idx].compare_exchange_weak(head, next,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            chunk = head;
            break;
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
    // Lock-free Treiber push.  Writing n->next touches the chunk page, which
    // after fork may be copy-on-write: with no lock held and preemption on, the
    // resulting COW page fault is legal (OSv forbids faulting non-preemptable).
    free_node *head = g_freelist[idx].load(std::memory_order_relaxed);
    do {
        n->next = head;
    } while (!g_freelist[idx].compare_exchange_weak(head, n,
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

} // namespace fork_arena

#endif // CONF_fork
