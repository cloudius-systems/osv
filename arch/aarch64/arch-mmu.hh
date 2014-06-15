/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_MMU_HH
#define ARCH_MMU_HH

#include <osv/debug.h>

/* AArch64 MMU
 *
 * For the AArch64 MMU we have many choices offered by the Architecture.
 * See boot.S for the choices set in the system registers.
 */

namespace mmu {
constexpr int max_phys_addr_size = 48;
constexpr int device_range_start = 0x8000000;
constexpr int device_range_stop = 0x10000000;

template<int N>
class pt_element : public pt_element_common<N> {
public:
    constexpr pt_element() noexcept : pt_element_common<N>(0) {}
    explicit pt_element(u64 x) noexcept : pt_element_common<N>(x) {}

    /* false->non-shareable true->Inner Shareable */
    inline void set_share(bool v) {
        x &= ~(3ul << 8);
        if (v)
            x |= (3ul << 8);
    }

    inline void set_attridx(unsigned char c) {
        assert(c <= 7);
        x &= ~(7ul << 2);
        x |= (c << 2);
    }
private:
    using pt_element_common<N>::x;
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
    return (N == 1 || N == 2) && (x & 0x3) == 0x1;
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
inline phys pt_element_common<N>::addr(bool large) const {
    u64 v = x & ((1ul << max_phys_addr_size) - 1);
    if (large)
        v &= ~0x1ffffful;
    else
        v &= ~0xffful;
    return v;
}

template<int N>
inline u64 pt_element_common<N>::pfn(bool large) const { return addr(large) >> page_size_shift; }
template<int N>
inline phys pt_element_common<N>::next_pt_addr() const { return addr(false); }
template<int N>
inline u64 pt_element_common<N>::next_pt_pfn() const { return pfn(false); }

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
pt_element<N> make_pte(phys addr, bool large, unsigned perm = perm_read | perm_write | perm_exec)
{
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

    if (addr >= mmu::device_range_start && addr < mmu::device_range_stop) {
        /* we need to mark device memory as such, because the
           semantics of the load/store instructions change */
        debug_early_u64("make_pte: device memory at ", (u64)addr);
        pte.set_attridx(0);
    } else {
        pte.set_attridx(4);
    }

    return pte;
}

template<int N>
pt_element<N> make_normal_pte(phys addr, unsigned perm = perm_read | perm_write | perm_exec)
{
    return make_pte<N>(addr, false, perm);
}

}
#endif /* ARCH_MMU_HH */
