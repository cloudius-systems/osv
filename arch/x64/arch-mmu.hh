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

namespace mmu {

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

inline phys pt_element::addr(bool large) const {
    auto v = x & ((u64(1) << (64-page_size_shift)) - 1);
    v &= ~u64(0xfff);
    v &= ~(u64(large) << page_size_shift);
    return v;
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

inline void pt_element::set_addr(phys addr, bool large) {
    auto mask = 0x8000000000000fff | (u64(large) << page_size_shift);
    x = (x & mask) | addr;
}

inline void pt_element::set_pfn(u64 pfn, bool large) {
    set_addr(pfn << page_size_shift, large);
}

}

#endif /* ARCH_MMU_HH_ */
