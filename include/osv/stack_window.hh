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
#include <osv/kernel_config_memory_debug.h>

// Kernel thread and syscall stacks that do not need physically contiguous
// memory.  Under CONF_fork every kernel stack must be mapped identically in
// every address space, which the linear map gives but only for contiguous
// physical ranges; once physical memory fragments, a 64 KiB or 256 KiB stack
// can no longer be found there even with plenty of memory free.  The stack
// window is a range of kernel virtual addresses under a top-level page-table
// entry that fork() shares verbatim, backed page by page, so a stack mapped
// there is coherent across address spaces without being contiguous.
//
// A window stack is not physically contiguous, so a buffer on it that spans a
// page boundary must be translated page by page before a device sees it.
// Stack buffers do reach devices: virtio-net's control-queue commands live on
// the stack.  That is safe because vring::add_sg() translates each page with
// mmu::virt_to_phys(vaddr, len, ...), which walks window addresses, and
// because those commands run at driver init, before init() makes the window
// usable.  A driver that translates a whole buffer with one virt_to_phys() (or
// BSD vtophys()) must not be given stack memory.  Callers try the linear map
// first and use the window only when that would block.
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

// Under CONF_memory_debug there is no window, and the debug allocator, which
// never reuses an address, may eventually hand out this range itself.
inline bool contains(const void* p)
{
    auto a = reinterpret_cast<uintptr_t>(p);
    return !CONF_memory_debug && a >= base && a < base + size;
}

}
}

#endif
