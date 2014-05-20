/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_MMU_HH_
#define ARCH_MMU_HH_

#include <osv/ilog2.hh>
#include <osv/types.h>
#include <osv/mmu-defs.hh>
#include <api/assert.h>

namespace mmu {

extern uint8_t phys_bits, virt_bits;
constexpr uint8_t rsvd_bits_used = 0;
constexpr uint8_t max_phys_bits = 52 - rsvd_bits_used;

constexpr uint64_t pte_addr_mask(bool large)
{
    return ((1ull << max_phys_bits) - 1) & ~(0xfffull) & ~(uint64_t(large) << page_size_shift);
}

class arch_pt_element {
public:
    arch_pt_element() = delete;
    static inline bool user(pt_element *e) { return e->x & 4; }
    static inline bool accessed(pt_element *e) { return e->x & 0x20; }
    static inline void set_user(pt_element *e, bool v) { e->set_bit(2, v); }
    static inline void set_accessed(pt_element *e, bool v) { e->set_bit(5, v); }
};

/* common interface implementation */

inline bool pt_element::empty() const { return !x; }
inline bool pt_element::valid() const { return x & 1; }
inline bool pt_element::writable() const { return x & 2; }
inline bool pt_element::executable() const { return !(x >> 63); } /* NX */
inline bool pt_element::dirty() const { return x & 0x40; }
inline bool pt_element::large() const { return x & 0x80; }

inline bool pt_element::sw_bit(unsigned off) const {
    assert(off < 10);
    return (x >> (53 + off)) & 1;
}

inline phys pt_element::addr(bool large) const {
    return x & pte_addr_mask(large);
}

inline u64 pt_element::pfn(bool large) const {
    return addr(large) >> page_size_shift;
}

inline phys pt_element::next_pt_addr() const { return addr(false); }
inline u64 pt_element::next_pt_pfn() const { return pfn(false); }

inline void pt_element::set_valid(bool v) { set_bit(0, v); }
inline void pt_element::set_writable(bool v) { set_bit(1, v); }
inline void pt_element::set_executable(bool v) { set_bit(63, !v); } /* NX */
inline void pt_element::set_dirty(bool v) { set_bit(6, v); }
inline void pt_element::set_large(bool v) { set_bit(7, v); }

inline void pt_element::set_sw_bit(unsigned off, bool v) {
    assert(off < 10);
    set_bit(53 + off, v);
}

inline void pt_element::set_addr(phys addr, bool large) {
    x = (x & ~pte_addr_mask(large)) | addr;
}

inline void pt_element::set_pfn(u64 pfn, bool large) {
    set_addr(pfn << page_size_shift, large);
}

}

#endif /* ARCH_MMU_HH_ */
