/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MMU_DEFS_HH
#define MMU_DEFS_HH

#include <stdint.h>

namespace mmu {

constexpr uintptr_t page_size = 4096;
constexpr int page_size_shift = 12; // log2(page_size)

constexpr int pte_per_page = 512;
constexpr int pte_per_page_shift = 9; // log2(pte_per_page)

constexpr uintptr_t huge_page_size = mmu::page_size*pte_per_page; // 2 MB

typedef uint64_t f_offset;
typedef uint64_t phys;

static char* const phys_mem = reinterpret_cast<char*>(0xffffc00000000000);
// area for debug allocations:
static char* const debug_base = reinterpret_cast<char*>(0xffffe00000000000);

enum {
    perm_read = 1,
    perm_write = 2,
    perm_exec = 4,
    perm_rx = perm_read | perm_exec,
    perm_rw = perm_read | perm_write,
    perm_rwx = perm_read | perm_write | perm_exec,
};

enum {
    page_fault_prot  = 1ul << 0,
    page_fault_write = 1ul << 1,
    page_fault_user  = 1ul << 2,
    page_fault_rsvd  = 1ul << 3,
    page_fault_insn  = 1ul << 4,
};

enum {
    mmap_fixed       = 1ul << 0,
    mmap_populate    = 1ul << 1,
    mmap_shared      = 1ul << 2,
    mmap_uninitialized = 1ul << 3,
    mmap_jvm_heap    = 1ul << 4,
    mmap_small       = 1ul << 5,
    mmap_jvm_balloon = 1ul << 6,
};

}
#endif
