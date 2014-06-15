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

constexpr uint64_t pte_addr_mask(bool large)
{
    return ((1ull << max_phys_bits) - 1) & ~(0xfffull) & ~(uint64_t(large) << page_size_shift);
}

class pt_element : public pt_element_common {
public:
    constexpr pt_element() noexcept : pt_element_common(0) {}
    explicit pt_element(u64 x) noexcept : pt_element_common(x) {}
};

/* common interface implementation */

inline bool pt_element_common::empty() const { return !x; }
inline bool pt_element_common::valid() const { return x & 1; }
inline bool pt_element_common::writable() const { return x & 2; }
inline bool pt_element_common::executable() const { return !(x >> 63); } /* NX */
inline bool pt_element_common::dirty() const { return x & 0x40; }
inline bool pt_element_common::large() const { return x & 0x80; }
inline bool pt_element_common::user() { return x & 4; }
inline bool pt_element_common::accessed() { return x & 0x20; }

inline bool pt_element_common::sw_bit(unsigned off) const {
    assert(off < 10);
    return (x >> (53 + off)) & 1;
}

inline bool pt_element_common::rsvd_bit(unsigned off) const {
    assert(off < rsvd_bits_used);
    return (x >> (51 - off)) & 1;
}

inline phys pt_element_common::addr(bool large) const {
    return x & pte_addr_mask(large);
}

inline u64 pt_element_common::pfn(bool large) const {
    return addr(large) >> page_size_shift;
}

inline phys pt_element_common::next_pt_addr() const { return addr(false); }
inline u64 pt_element_common::next_pt_pfn() const { return pfn(false); }

inline void pt_element_common::set_valid(bool v) { set_bit(0, v); }
inline void pt_element_common::set_writable(bool v) { set_bit(1, v); }
inline void pt_element_common::set_executable(bool v) { set_bit(63, !v); } /* NX */
inline void pt_element_common::set_dirty(bool v) { set_bit(6, v); }
inline void pt_element_common::set_large(bool v) { set_bit(7, v); }
inline void pt_element_common::set_user(bool v) { set_bit(2, v); }
inline void pt_element_common::set_accessed(bool v) { set_bit(5, v); }

inline void pt_element_common::set_sw_bit(unsigned off, bool v) {
    assert(off < 10);
    set_bit(53 + off, v);
}

inline void pt_element_common::set_rsvd_bit(unsigned off, bool v) {
    assert(off < rsvd_bits_used);
    set_bit(51 - off, v);
}

inline void pt_element_common::set_addr(phys addr, bool large) {
    x = (x & ~pte_addr_mask(large)) | addr;
}

inline void pt_element_common::set_pfn(u64 pfn, bool large) {
    set_addr(pfn << page_size_shift, large);
}
}
#endif /* ARCH_MMU_HH_ */
