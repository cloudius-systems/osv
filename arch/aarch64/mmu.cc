/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mmu.hh>
#include <osv/prio.hh>
#include <osv/sched.hh>
#include <osv/debug.h>
#include <osv/irqlock.hh>
#include <osv/kernel_config_logger_debug.h>

#include "arch-cpu.hh"
#include "exceptions.hh"

#define ACCESS_FLAG_FAULT_LEVEL_3(esr)            ((esr & 0b0111111) == 0x0b) // 0xb = 0b1011 indicates level 3
#define ACCESS_FLAG_FAULT_LEVEL_3_WHEN_WRITE(esr) ((esr & 0b1111111) == 0x4b)

TRACEPOINT(trace_mmu_vm_access_flag_fault, "addr=%p", void *);

template <typename T>
T* phys_to_virt_cast(mmu::phys pa)
{
    void *virt = mmu::phys_mem + pa;
    return static_cast<T*>(virt);
}

static void handle_access_flag_fault(exception_frame *ef, u64 addr) {
    trace_mmu_vm_access_flag_fault((void*)addr);

    // The access bit of a PTE (Page Table Entry) at level 3 got cleared and we need
    // to set it to handle this page fault. Therefore we need to do a page walk
    // to navigate down to the level 3 and identify relevant PTE.

    // Start with root PTE
    auto root_pt = mmu::get_root_pt(addr);
    auto root_ptep = mmu::hw_ptep<4>::force(root_pt);

    // Identify PTEP (PTE Pointer) at level 0 (the template parameter is reversed)
    // First identify the ptep table at this level
    auto l3_ptep_table = mmu::hw_ptep<3>::force(phys_to_virt_cast<mmu::pt_element<3>>(root_ptep.read().next_pt_addr()));
    // Then access ptep at the index encoded in the virtual address
    auto l3_ptep = l3_ptep_table.at(mmu::pt_index(reinterpret_cast<void*>(addr), 3));

    // Identify PTEP at level 1 (first identify the ptep table and then the relevant ptep)
    auto l2_ptep_table = mmu::hw_ptep<2>::force(phys_to_virt_cast<mmu::pt_element<2>>(l3_ptep.read().next_pt_addr()));
    auto l2_ptep = l2_ptep_table.at(mmu::pt_index(reinterpret_cast<void*>(addr), 2));

    // Identify PTEP at level 2 (first identify the ptep table and then the relevant ptep)
    auto l1_ptep_table = mmu::hw_ptep<1>::force(phys_to_virt_cast<mmu::pt_element<1>>(l2_ptep.read().next_pt_addr()));
    auto l1_ptep = l1_ptep_table.at(mmu::pt_index(reinterpret_cast<void*>(addr), 1));

    // Identify PTEP at level 3 (first identify the ptep table and then the relevant ptep)
    auto l0_ptep_table = mmu::hw_ptep<0>::force(phys_to_virt_cast<mmu::pt_element<0>>(l1_ptep.read().next_pt_addr()));
    auto l0_ptep = l0_ptep_table.at(mmu::pt_index(reinterpret_cast<void*>(addr), 0));

    // Read leaf PTE
    auto leaf_pte = l0_ptep.read();

    leaf_pte.set_accessed(true);
    if (ACCESS_FLAG_FAULT_LEVEL_3_WHEN_WRITE(ef->esr)) {
        leaf_pte.set_dirty(true);
    }

    l0_ptep.write(leaf_pte);
    mmu::synchronize_page_table_modifications();
}

void page_fault(exception_frame *ef)
{
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);
#if CONF_logger_debug
    debug_early_entry("page_fault");
#endif
    u64 addr;
    asm volatile ("mrs %0, far_el1" : "=r"(addr));
#if CONF_logger_debug
    debug_early_u64("faulting address ", (u64)addr);
    debug_early_u64("elr exception ra ", (u64)ef->elr);
#endif

    if (fixup_fault(ef)) {
#if CONF_logger_debug
        debug_early("fixed up with fixup_fault\n");
#endif
        return;
    }

    if (!ef->elr) {
        abort("trying to execute null pointer");
    }

    if (ACCESS_FLAG_FAULT_LEVEL_3(ef->esr)) {
        return handle_access_flag_fault(ef, addr);
    }

    /* vm_fault might sleep, so check that the thread is preemptable,
     * and that interrupts in the saved pstate are enabled.
     * Then enable interrupts for the vm_fault.
     */
    assert(sched::preemptable());
    assert(!(ef->spsr & processor::daif_i));

    DROP_LOCK(irq_lock) {
        mmu::vm_fault(addr, ef);
    }

#if CONF_logger_debug
    debug_early("leaving page_fault()\n");
#endif
}

namespace mmu {

void flush_tlb_all() {
    asm volatile("dsb sy; tlbi vmalle1is; dsb sy; isb;");
}

void flush_tlb_local() {
    asm volatile("dsb sy; tlbi vmalle1; dsb sy; isb;");
}

static pt_element<4> page_table_root[2] __attribute__((init_priority((int)init_prio::pt_root)));
u64 mem_addr;

extern "C" { /* see boot.S */
    extern u64 smpboot_ttbr0;
    extern u64 smpboot_ttbr1;
}

void switch_to_runtime_page_tables()
{
    auto pt_ttbr0 = smpboot_ttbr0 = mmu::page_table_root[0].next_pt_addr();
    auto pt_ttbr1 = smpboot_ttbr1 = mmu::page_table_root[1].next_pt_addr();
    asm volatile("msr ttbr0_el1, %0; isb;" ::"r" (pt_ttbr0));
    asm volatile("msr ttbr1_el1, %0; isb;" ::"r" (pt_ttbr1));
    mmu::flush_tlb_all();
}

#if CONF_fork
// --- fork COW arch hooks (aarch64) --------------------------------------
//
// On aarch64 OSv uses a dual-TTBR split (see arch/aarch64/boot.S TCR_EL1):
//   TTBR0_EL1 -> 0x0000_0000_0000_0000 .. 0x0000_ffff_ffff_ffff (low half)
//   TTBR1_EL1 -> 0xffff_0000_0000_0000 .. high half
// Unlike a hypothetical user/kernel split, OSv maps EVERYTHING it uses in the
// LOW half (TTBR0): the kernel ELF (OSV_KERNEL_VM_BASE, ~63 GB), the linear
// physical map (main_mem_area_base 0x400000000000), the application and all
// mmap VMAs.  The high half (TTBR1) is effectively unused today.  So the fork
// per-child COW root is a LOW-half (TTBR0) root: page_table_root[0] is the
// kernel/AS0 TTBR0 root, and a child's private COW root replaces TTBR0 while
// TTBR1 (page_table_root[1]) stays shared.  This mirrors x86's single-CR3 COW
// clone, because the aarch64 top-level (L0) slot layout is identical to x86's
// PML4 layout (kernel ELF in slot 0, app in slots 32..127, phys map in slots
// 128.., all from the arch-neutral mmu-defs.hh / elf.hh constants), so the
// generic clone in core/mmu.cc (slots pml4_app_first=1..pml4_app_last=127 COW,
// slot 0 and 128.. shared) is correct unchanged.

// Virtual pointer to the current thread's low-half (TTBR0) root: the child
// AS's private COW root when running in a forked child, else the kernel
// (AS0) TTBR0 root.  Defined in core/mmu.cc.
pt_element<4> *current_pt_root();

pt_element<4> *get_root_pt(uintptr_t virt)
{
    // High half (TTBR1): always the single shared kernel root (unused today,
    // but keep the static root so any future high-half mapping is shared).
    if (virt >> 63) {
        return &page_table_root[1];
    }
    // Low half (TTBR0): the current thread's address-space root (per-AS COW
    // root for a fork child, else the kernel/AS0 TTBR0 root).
    return current_pt_root();
}

// Virtual pointer to the kernel (AS0) low-half (TTBR0) root -- the template the
// per-child COW root is cloned from (core/mmu.cc clone_address_space).
pt_element<4> *kernel_pml4()
{
    return &page_table_root[0];
}

// Physical address of the kernel (AS0) TTBR0 root -- the TTBR0_EL1 value that
// maps the kernel ELF + linear phys map.  Used as the shared base for cloned
// child address spaces and as AS0's pt_root (core/mmu.cc pt_root_phys).
phys kernel_pt_root_phys()
{
    return page_table_root[0].next_pt_addr();
}
#else
pt_element<4> *get_root_pt(uintptr_t virt)
{
    return &page_table_root[virt >> 63];
}
#endif // CONF_fork

bool is_page_fault_insn(unsigned int esr) {
    unsigned int ec = esr >> 26;
    return ec == 0x20 || ec == 0x21;
}

bool is_page_fault_write(unsigned int esr) {
    unsigned int ec = esr >> 26;
    return (ec == 0x24 || ec == 0x25) && (esr & 0x40);
}

bool is_page_fault_write_exclusive(unsigned int esr) {
    return is_page_fault_write(esr);
}

bool fast_sigsegv_check(uintptr_t addr, exception_frame* ef) {
    return false;
}

void synchronize_cpu_caches(void *v, size_t size) {
    // The aarch64 qualifies as Modified Harvard architecture and defines separate
    // cpu instruction and data caches - I-cache and D-cache. Therefore it is necessary
    // to synchronize both caches by cleaning data cache and invalidating instruction
    // cache after loading code into memory before letting it be executed.
    // For more details of why and when it is necessary please read this excellent article -
    // https://community.arm.com/developer/ip-products/processors/b/processors-ip-blog/posts/caches-and-self-modifying-code
    // or this paper - https://hal.inria.fr/hal-02509910/document.
    //
    // So when OSv dynamic linker, being part of the kernel code, loads pages
    // of executable sections of ELF segments into memory, we need to clean D-cache
    // in order push code (as data) into next cache level (L2) and invalidate
    // the I-cache right before it gets executed.
    //
    // In order to achieve the above we delegate to the __clear_cache builtin.
    // The __clear_cache does following in terms of ARM64 assembly:
    //
    // For each D-cache line in the range (v, v + size):
    //   DC CVAU, Xn ; Clean data cache by virtual address (VA) to PoU
    // DSB ISH       ; Ensure visibility of the data cleaned from cache
    // For each I-cache line in the range (v, v + size):
    //   IC IVAU, Xn ; Invalidate instruction cache by VA to PoU
    // DSB ISH       ; Ensure completion of the invalidations
    // ISB           ; Synchronize the fetched instruction stream
    //
    // Please note that that both DC CVAU and IC CVAU are broadcast to all cores in the
    // same Inner Sharebility domain (which all OSv memory is mapped as) so that all
    // caches in all cores should eventually see and execute same code.
    //
    // For more details about what this built-in does, please read this gcc documentation -
    // https://gcc.gnu.org/onlinedocs/gcc/Other-Builtins.html
    __builtin___clear_cache((char*)v, (char*)(v + size));
}
}
