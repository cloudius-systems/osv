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
// WHOLE-ARENA PER-AS WINDOWS (leak #2 cure).  Every live address space owns one
// disjoint VA window in a single 512 GiB application slot (slot 97).  There is
// NO shared global bump: each AS bump-carves inside its own window and recycles
// through its own per-class free-list.  A window VA identifies its owning AS by
// pure BSS arithmetic, so a foreign free() is an arithmetic no-op that never
// reads the chunk header -- no pointer is ever recycled across address spaces,
// and the whole window is reclaimed wholesale by destroy_address_space on reap.
// ALL of the allocator's bookkeeping lives in kernel BSS, NOT inside arena
// pages -- so managing the arena never faults an arena page and never recurses
// back into malloc.
// -----------------------------------------------------------------------------
namespace fork_arena {

// Legacy shared-arena constants.  Slot 96 (96 << 39) held the retired shared
// global bump; it is used ONLY by the C2 negative-control build
// (-DFORK_SHARED_BUMP=1), which reintroduces the shared bump to prove the probe
// can fire.  The shipped cure carves nothing here.
constexpr uintptr_t arena_base = 96ull << 39;   // 0x300000000000
constexpr size_t    arena_size = 512ull << 20;  // 512 MiB (shared-bump control only)

// Per-AS window slot: a single 512 GiB application slot carved into one
// disjoint window per AS.  contains() uses the whole slot range (a cheap range
// test on the free/realloc dispatch path); the per-AS window inside it is
// decided by BSS arithmetic in free().
constexpr uintptr_t win_slot_base = 97ull << 39;   // 0x308000000000
constexpr uintptr_t win_slot_end  = 98ull << 39;   // 0x310000000000

// Set the requested PER-AS WINDOW size in MiB (clamped to [512 .. 2048] in
// init(): the floor is the historical arena size, the cap is one AS's share of
// the 512 GiB window slot).  Called by the loader from the --fork-arena-size-mb
// cmdline option BEFORE init() runs; read from the cmdline, not getenv, because
// init() runs before the application environment exists.  mb==0 keeps the
// default.  SEMANTIC CHANGE from the shared-arena era: this sizes each AS's
// PRIVATE VA window (no committed boot RAM), not a shared eagerly-populated
// arena.
void set_size_mb(unsigned long mb);

// One-time setup: resolve the per-AS window size (and, for the control build,
// reserve the shared arena).  Call once, after the SMP allocator is up, before
// the application runs.  Idempotent.
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

// True if `p` points inside the arena (an arena allocation).  Cheap range test
// over the per-AS window slot (97) and the legacy shared-arena slot (96).  Slot
// 96 is unused by the shipped cure (nothing is carved there), so the extra
// compare is a harmless constant cost that lets the C2 negative-control build
// (-DFORK_SHARED_BUMP=1, which DOES carve slot 96) dispatch its frees here
// without a cross-translation-unit define.
static inline bool contains(const void *p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    if (a >= win_slot_base && a < win_slot_end) {
        return true;
    }
    if (a >= arena_base && a < arena_base + arena_size) {
        return true;
    }
    return false;
}

// Allocate `size` bytes with `alignment` from the arena; nullptr if it cannot
// (too large, or arena exhausted -- caller falls back to the normal heap).
void *alloc(size_t size, size_t alignment);

// Free an arena allocation (p must satisfy contains(p)).
void free(void *p);

// Reclaim the per-address-space free-list slot for a dying fork child address
// space.  Called from mmu::destroy_address_space so a later process can reuse
// the slot.  @as is the opaque mmu::address_space* being destroyed.
void release_as(void *as);

// Usable size of an arena allocation (p must satisfy contains(p)).
size_t usable_size(void *p);

// Largest request the arena will service; larger requests fall through to the
// normal heap (huge allocations already go to app-slot mmap, which is COW-able).
constexpr size_t max_alloc = 2ull << 20;   // 2 MiB

} // namespace fork_arena

#endif // CONF_fork
#endif // FORK_ARENA_HH
