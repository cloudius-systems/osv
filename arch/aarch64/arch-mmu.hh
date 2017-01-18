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
// device_range_* are used only for debug purpose
constexpr int device_range_start = 0x8000000;
constexpr int device_range_stop = 0x40000000;
extern u64 mem_addr; /* set by the dtb_setup constructor */

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

static inline bool dbg_mem_is_dev(phys addr)
{
    return addr >= mmu::device_range_start && addr < mmu::device_range_stop;
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
        assert(!dbg_mem_is_dev(addr));
        pte.set_attridx(4);
        break;
    case mattr::dev:
        assert(dbg_mem_is_dev(addr));
        pte.set_attridx(0);
        break;
    }

    return pte;
}

}
#endif /* ARCH_MMU_HH */
