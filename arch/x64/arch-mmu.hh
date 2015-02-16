/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_MMU_HH_
#define ARCH_MMU_HH_

namespace mmu {
extern uint8_t phys_bits, virt_bits;
constexpr uint8_t rsvd_bits_used = 1;
constexpr uint8_t max_phys_bits = 52 - rsvd_bits_used;

enum class mattr {
    normal
};
constexpr mattr mattr_default = mattr::normal;

constexpr uint64_t pte_addr_mask(bool large)
{
    return ((1ull << max_phys_bits) - 1) & ~(0xfffull) & ~(uint64_t(large) << page_size_shift);
}

template<int N>
class pt_element : public pt_element_common<N> {
public:
    constexpr pt_element() noexcept : pt_element_common<N>(0) {}
    explicit pt_element(u64 x) noexcept : pt_element_common<N>(x) {}
};

/* common interface implementation */

template<int N>
inline bool pt_element_common<N>::empty() const { return !x; }
template<int N>
inline bool pt_element_common<N>::valid() const { return x & 1; }
template<int N>
inline bool pt_element_common<N>::writable() const { return x & 2; }
template<int N>
inline bool pt_element_common<N>::executable() const { return !(x >> 63); } /* NX */
template<int N>
inline bool pt_element_common<N>::dirty() const {
    static_assert(pt_level_traits<N>::leaf_capable::value, "only leaf pte can be dirty");
    assert(!pt_level_traits<N>::large_capable::value || large());
    return x & 0x40;
}
template<int N>
inline bool pt_element_common<N>::large() const {
    return pt_level_traits<N>::large_capable::value && (x & 0x80);
}
template<int N>
inline bool pt_element_common<N>::user() { return x & 4; }
template<int N>
inline bool pt_element_common<N>::accessed() { return x & 0x20; }

template<int N>
inline bool pt_element_common<N>::sw_bit(unsigned off) const {
    assert(off < 10);
    return (x >> (53 + off)) & 1;
}

template<int N>
inline bool pt_element_common<N>::rsvd_bit(unsigned off) const {
    assert(off < rsvd_bits_used);
    return (x >> (51 - off)) & 1;
}

template<int N>
inline phys pt_element_common<N>::addr() const {
    return x & pte_addr_mask(large());
}

template<int N>
inline u64 pt_element_common<N>::pfn() const {
    return addr() >> page_size_shift;
}

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
inline void pt_element_common<N>::set_writable(bool v) { set_bit(1, v); }
template<int N>
inline void pt_element_common<N>::set_executable(bool v) { set_bit(63, !v); } /* NX */
template<int N>
inline void pt_element_common<N>::set_dirty(bool v) { set_bit(6, v); }
template<int N>
inline void pt_element_common<N>::set_large(bool v) { set_bit(7, v); }
template<int N>
inline void pt_element_common<N>::set_user(bool v) { set_bit(2, v); }
template<int N>
inline void pt_element_common<N>::set_accessed(bool v) { set_bit(5, v); }

template<int N>
inline void pt_element_common<N>::set_sw_bit(unsigned off, bool v) {
    assert(off < 10);
    set_bit(53 + off, v);
}

template<int N>
inline void pt_element_common<N>::set_rsvd_bit(unsigned off, bool v) {
    assert(off < rsvd_bits_used);
    set_bit(51 - off, v);
}

template<int N>
inline void pt_element_common<N>::set_addr(phys addr, bool large) {
    x = (x & ~pte_addr_mask(large)) | addr;
}

template<int N>
inline void pt_element_common<N>::set_pfn(u64 pfn, bool large) {
    set_addr(pfn << page_size_shift, large);
}

// Currently mem_attr is ignored on x86_64
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
    pte.set_user(true);
    pte.set_accessed(true);

    return pte;
}

}
#endif /* ARCH_MMU_HH_ */
