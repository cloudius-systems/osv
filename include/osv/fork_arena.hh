/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef FORK_ARENA_HH
#define FORK_ARENA_HH

#include <osv/kernel_config_fork.h>

#if CONF_fork

#include <cstddef>
#include <cstdint>

// -----------------------------------------------------------------------------
// Fork heap arena (CONF_fork only).
//
// OSv's normal small-object heap lives in the KERNEL IDENTITY MAP
// (mem_area::mempool, VA >= 0x400000000000).  The identity map is shared
// VERBATIM across every address space (the kernel needs virt_to_phys on it to
// be pure arithmetic for DMA), so it CANNOT be copy-on-write cloned on fork().
// A forked child that writes to a heap object inherited from its parent would
// therefore scribble the SHARED heap and corrupt the parent.
//
// The fork arena gives APPLICATION allocations a home in ordinary anonymous
// mappings in the app-slot VA range (below 0x400000000000).  They are
// page-table-mapped like any private writable vma, so clone_address_space()'s
// COW machinery isolates them per child for free: the child gets its own
// copy-on-write copy of the whole app heap, writes diverge privately, and the
// parent is unaffected.  virt_to_phys() already walks the page table for
// addresses below phys_mem, so arena pages remain DMA-usable.
//
// The arena range is PML4 slots 96..127 (16 TiB), cut into 64 MiB extents.
// Each extent is owned by at most one live address space at a time, recorded
// in an identity-mapped owner table, so a free() decides ownership without
// reading arena memory.  Per-process state hangs off the address space and is
// freed with it; there is no limit on the number of processes other than
// memory.  mmu::find_hole() keeps general mmap out of the range, so contains()
// is exact.
// -----------------------------------------------------------------------------
namespace fork_arena {

constexpr uintptr_t arena_base = 96ull << 39;    // 0x300000000000
constexpr uintptr_t arena_end  = 128ull << 39;   // 0x400000000000
constexpr unsigned  extent_shift = 26;           // 64 MiB extents
constexpr size_t    extent_size = size_t(1) << extent_shift;
constexpr size_t    num_extents = (arena_end - arena_base) >> extent_shift;

// Accepted and ignored: an address space has no fixed window left to size.
void set_size_mb(unsigned long mb);

// One-time setup: allocate the extent tables.  Call once, after the SMP
// allocator is up, before the application runs.  Idempotent.
void init();

// True once init() has run and routing is live.
bool ready();

// Per-thread "force kernel heap" depth.  While > 0, an app thread's
// allocations are routed to the normal identity kernel heap instead of the
// arena.  Kernel entry points that allocate structures which MUST be visible
// at the same VA in every address space (thread objects, wait_records, FILE
// structs on the shared stdio open-file list, ...) wrap those allocations in a
// kernel_heap_scope so a forked child does not get a COW-private copy of a
// globally-shared kernel object.
//
// VOLATILE is load-bearing, not decoration.  GCC models the C library
// allocators (malloc/aligned_alloc/...) as builtins that neither read nor
// write global memory.  Around such a call a non-volatile ++force_kernel_heap /
// --force_kernel_heap pair is dead (no observer between them) and GCC ELIDES
// it -- turning a kernel_heap_scope that wraps only an aligned_alloc() into a
// no-op.  That is exactly how the app main thread's TLS block leaked into the
// COW fork arena: setup_tcb()'s scope vanished, the TLS landed in the arena,
// clone_address_space() COW-write-protected it, and the forking parent
// triple-faulted on its next ++preempt_counter (a %fs TLS write).  volatile
// forces every read and write to be real memory accesses, so the guard
// survives even around a builtin-modeled allocator.
extern volatile __thread unsigned force_kernel_heap;

struct kernel_heap_scope {
    kernel_heap_scope()  { ++force_kernel_heap; }
    ~kernel_heap_scope() { --force_kernel_heap; }
};

// True if `p` points inside the arena range (an arena allocation).
static inline bool contains(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= arena_base && a < arena_end;
}

// Allocate `size` bytes with `alignment` from the current address space's
// arena.  nullptr if the request is outside the arena's size classes, the
// caller cannot touch arena memory (preemption or irqs off), or memory or
// arena VA is exhausted (errno is then ENOMEM).
void *alloc(size_t size, size_t alignment);

// True if alloc() serves a request of this size and alignment, so a nullptr
// from it means exhaustion rather than "not an arena request".
bool serves(size_t size, size_t alignment);

// Free an arena allocation (p must satisfy contains(p)).  A free from any
// address space other than the owner is a no-op that reads no arena memory.
void free(void *p);

// Release the arena state of a dying address space: its extents go back to
// the pool and its state is freed.  Called from mmu::destroy_address_space
// after the page tables are gone.  @as is the opaque mmu::address_space*.
void release_as(void *as);

// Usable size of an arena allocation (p must satisfy contains(p)).
size_t usable_size(void *p);

// Largest request the arena will service; larger requests fall through to a
// per-address-space mapping (still COW-isolated, freed at exit).
constexpr size_t max_alloc = 2ull << 20;   // 2 MiB
static_assert(max_alloc < extent_size, "a chunk must fit inside one extent");

// Counters, for tests and diagnosis.
struct arena_stats {
    unsigned long live_states;      // address spaces with arena state
    unsigned long extents_in_use;   // extents owned by a live address space
    unsigned long committed_bytes;  // arena regions mapped in live address spaces
    unsigned long foreign_frees;    // frees skipped: not owned by the freeing AS
    unsigned long atomic_retained;  // frees skipped: preemption or irqs off
};
arena_stats stats();
#define FORK_ARENA_HAS_STATS 1

} // namespace fork_arena

#endif // CONF_fork
#endif // FORK_ARENA_HH
