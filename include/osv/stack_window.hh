/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef OSV_STACK_WINDOW_HH
#define OSV_STACK_WINDOW_HH

#include <cstddef>
#include <cstdint>

// Kernel thread and syscall stacks that do not need physically contiguous
// memory.  Under CONF_fork every kernel stack must be mapped identically in
// every address space, which the linear map gives but only for contiguous
// physical ranges; once physical memory fragments, a 64 KiB or 256 KiB stack
// can no longer be found there even with plenty of memory free.  The stack
// window is a range of kernel virtual addresses under a top-level page-table
// entry that fork() shares verbatim, backed page by page, so a stack mapped
// there is coherent across address spaces without being contiguous.
//
// Stacks are never DMA targets, which is what makes non-contiguous backing
// safe for them.  Callers try the linear map first and use the window only
// when that would block.
namespace mmu {
namespace stack_window {

// The window: the top 512 GiB of the debug memory area (top-level slot 254).
// The debug area is unused unless CONF_memory_debug, and slot 254 is a kernel
// slot that clone_address_space() shares between all address spaces.
constexpr uintptr_t base = 254ull << 39;
constexpr size_t size = 1ull << 39;

// Create the window's second-level table in the kernel root.  Must run before
// the first fork(): a child's top-level entries are copies of its parent's, so
// a table created later would be missing from children forked earlier.
void init();

// Map a stack of `bytes` (a page multiple) and return it, or nullptr if the
// window was never initialized or is full.  Never blocks on physical
// contiguity.
void* alloc(size_t bytes);

// Unmap a stack returned by alloc().  The caller guarantees no thread runs on
// it any more.
void free(void* p, size_t bytes);

inline bool contains(const void* p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return a >= base && a < base + size;
}

}
}

#endif
