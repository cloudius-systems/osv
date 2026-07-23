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
// The fork arena gives APPLICATION allocations a home in an ordinary anonymous
// mmap region in the app-slot VA range (below 0x400000000000).  That region is
// page-table-mapped like any private writable vma, so clone_address_space()'s
// COW machinery isolates it per child for free: the child gets its own
// copy-on-write copy of the whole app heap, writes diverge privately, and the
// parent is unaffected.  virt_to_phys() already walks the page table for
// addresses below phys_mem, so arena pages remain DMA-usable.
//
// The allocator is a simple segregated free-list.  ALL of its bookkeeping
// (per-size-class free-list heads, the bump pointer) lives in kernel BSS, NOT
// inside arena pages -- so managing the arena never faults an arena page and
// never recurses back into malloc (the recursive-fault trap the first arena
// prototype hit during fork's own page-table work).
// -----------------------------------------------------------------------------
namespace fork_arena {

// Fixed base and size of the arena's app-slot VA reservation.  Slot 96
// (96 << 39) is clear of the ELF load slot (32) and the default mmap hole
// (which grows up from slot 64).
constexpr uintptr_t arena_base = 96ull << 39;   // 0x300000000000
constexpr size_t    arena_size = 512ull << 20;  // 512 MiB of VA (lazily faulted)

// One-time setup: reserve the arena VA as an anonymous app-slot mapping.  Call
// once, after the SMP allocator is up, before the application runs.  Idempotent.
void init();

// True once init() has reserved the arena and routing is live.
bool ready();

// Per-thread "force kernel heap" depth.  While > 0, an app thread's
// allocations are routed to the normal identity kernel heap instead of the
// arena.  Kernel entry points that allocate structures which MUST be visible
// at the same VA in every address space (thread objects, wait_records, ...)
// wrap those allocations in a kernel_heap_scope so a forked child does not get
// a COW-private copy of a globally-shared kernel object.
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

// True if `p` points inside the arena (an arena allocation).  Cheap range test.
static inline bool contains(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= arena_base && a < arena_base + arena_size;
}

// Allocate `size` bytes with `alignment` from the arena; nullptr if it cannot
// (too large, or arena exhausted -- caller falls back to the normal heap).
void *alloc(size_t size, size_t alignment);

// Free an arena allocation (p must satisfy contains(p)).
void free(void *p);

// Usable size of an arena allocation (p must satisfy contains(p)).
size_t usable_size(void *p);

// Largest request the arena will service; larger requests fall through to the
// normal heap (huge allocations already go to app-slot mmap, which is COW-able).
constexpr size_t max_alloc = 2ull << 20;   // 2 MiB

} // namespace fork_arena

#endif // CONF_fork
#endif // FORK_ARENA_HH
