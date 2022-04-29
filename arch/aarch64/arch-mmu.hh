/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_MMU_HH
#define ARCH_MMU_HH

#include <osv/debug.hh>

/* AArch64 MMU
 *
 * For the AArch64 MMU we have many choices offered by the Architecture.
 * See boot.S for the choices set in the system registers.
 */

namespace mmu {
constexpr int max_phys_addr_size = 48;
extern u64 mem_addr; /* set by the dtb_setup constructor */
extern void *elf_phys_start;

enum class mattr {
    normal,
    dev
};
constexpr mattr mattr_default = mattr::normal;

template<int N>
class pt_element : public pt_element_common<N> {
public:
    constexpr pt_element() noexcept : pt_element_common<N>(0) {}
    explicit pt_element(u64 x) noexcept : pt_element_common<N>(x) {}

    /* false->non-shareable true->Inner Shareable */
    inline void set_share(bool v) {
        auto& x=pt_element_common<N>::x;
        x &= ~(3ul << 8);
        if (v)
            x |= (3ul << 8);
    }

    // mair_el1 register defines values for each 8 indexes. See boot.S
    inline void set_attridx(unsigned char c) {
        auto& x=pt_element_common<N>::x;
        assert(c <= 7);
        x &= ~(7ul << 2);
        x |= (c << 2);
    }
};


/* common interface implementation */
template<int N>
inline bool pt_element_common<N>::empty() const { return !x; }
template<int N>
inline bool pt_element_common<N>::valid() const { return x & 0x1; }
template<int N>
inline bool pt_element_common<N>::writable() const { return !(x & (1ul << 7)); } // AP[2]
template<int N>
inline bool pt_element_common<N>::executable() const { return !(x & (1ul << 53)); } // Priv. Execute Never
template<int N>
inline bool pt_element_common<N>::dirty() const { return x & (1ul << 55); } // Software Use[0]
template<int N>
inline bool pt_element_common<N>::large() const {
    return pt_level_traits<N>::large_capable::value && (x & 0x3) == 0x1;
}
template<int N>
inline bool pt_element_common<N>::user() { return x & (1 << 6); } // AP[1]
template<int N>
inline bool pt_element_common<N>::accessed() { return x & (1 << 10); } // AF

template<int N>
inline bool pt_element_common<N>::sw_bit(unsigned off) const {
    assert(off < 3);
    return (x >> (56 + off)) & 1;
}

template<int N>
inline bool pt_element_common<N>::rsvd_bit(unsigned off) const {
    return false;
}

template<int N>
inline phys pt_element_common<N>::addr() const {
    u64 v = x & ((1ul << max_phys_addr_size) - 1);
    if (large())
        v &= ~0x1ffffful;
    else
        v &= ~0xffful;
    return v;
}

template<int N>
inline u64 pt_element_common<N>::pfn() const { return addr() >> page_size_shift; }
template<int N>
inline phys pt_element_common<N>::next_pt_addr() const {
    assert(!large());
    return addr();
}
template<int N>
inline u64 pt_element_common<N>::next_pt_pfn() const {
    assert(!large());
    return pfn();
}

template<int N>
inline void pt_element_common<N>::set_valid(bool v) { set_bit(0, v); }
template<int N>
inline void pt_element_common<N>::set_writable(bool v) { set_bit(7, !v); } // AP[2]
template<int N>
inline void pt_element_common<N>::set_executable(bool v) { set_bit(53, !v); } // Priv. Execute Never
template<int N>
inline void pt_element_common<N>::set_dirty(bool v) { set_bit(55, v); }
template<int N>
inline void pt_element_common<N>::set_large(bool v) { set_bit(1, !v); }
template<int N>
inline void pt_element_common<N>::set_user(bool v) { set_bit(6, v); } // AP[1]
template<int N>
inline void pt_element_common<N>::set_accessed(bool v) { set_bit(10, v); } // AF

template<int N>
inline void pt_element_common<N>::set_sw_bit(unsigned off, bool v) {
    assert(off < 3);
    set_bit(56 + off, v);
}

template<int N>
inline void pt_element_common<N>::set_rsvd_bit(unsigned off, bool v) {
}

template<int N>
inline void pt_element_common<N>::set_addr(phys addr, bool large) {
    u64 mask = large ? 0xffff0000001ffffful : 0xffff000000000ffful;
    x = (x & mask) | (addr & ~mask);
    x |= large ? 1 : 3;
}

template<int N>
inline void pt_element_common<N>::set_pfn(u64 pfn, bool large) {
    set_addr(pfn << page_size_shift, large);
}

template<int N>
pt_element<N> make_pte(phys addr, bool leaf, unsigned perm = perm_rwx,
                       mattr mem_attr = mattr_default)
{
    assert(pt_level_traits<N>::leaf_capable::value || !leaf);
    bool large = pt_level_traits<N>::large_capable::value && leaf;
    pt_element<N> pte;
    pte.set_valid(perm != 0);
    pte.set_writable(perm & perm_write);
    pte.set_executable(perm & perm_exec);
    pte.set_dirty(true);
    pte.set_large(large);
    pte.set_addr(addr, large);

    pte.set_user(false);
    pte.set_accessed(true);
    pte.set_share(true);

    // we need to mark device memory as such, because the
    // semantics of the load/store instructions change
    switch (mem_attr) {
    default:
    case mattr::normal:
        pte.set_attridx(4);
        break;
    case mattr::dev:
        pte.set_attridx(0);
        break;
    }

    return pte;
}

// On aarch64 architecture which comes with weak memory model,
// it is necessary to force completion of writes to page table entries
// and flush cpu pipeline to make sure that subsequent accesses to the
// mapped memory areas do not cause page faults during page table walk later.
// The fragment from the "ARM v8 Architecture Reference Manual" (D5-2557) states
// this:
// "A translation table walk is considered to be a separate observer.
// An explicit write to the translation tables might be observed by that separate observer for either of the following:
// - A translation table walk caused by a different explicit write generated by the same instruction.
// - A translation table walk caused by an explicit access generated by any instruction appearing
//   in program order after the instruction doing the explicit write to the translation table.
// The explicit write to the translation tables is guaranteed to be observable, to the extent
// required by the shareability attributes, only after the execution of a DSB instruction.
// This DSB instruction and the instruction that performed the explicit write to the translation
// tables must have been executed by the same PE.
// Any writes to the translation tables are not observed by the translation table walks of
// an explicit memory access generated by a load or store that occurs in program order
// before the instruction that performs the write to the translation tables."
inline void synchronize_page_table_modifications() {
    asm volatile("dsb ishst; isb;");
}

}
#endif /* ARCH_MMU_HH */
