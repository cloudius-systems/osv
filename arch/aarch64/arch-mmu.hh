/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_MMU_HH
#define ARCH_MMU_HH

#include <osv/ilog2.hh>
#include <osv/types.h>
#include <osv/mmu-defs.hh>
#include <assert.h>

/* AArch64 MMU
 *
 * For the AArch64 MMU we have many choices offered by the Architecture.
 * See boot.S for the choices set in the system registers.
 */

namespace mmu {
constexpr int max_phys_addr_size = 48;
constexpr int device_range_start = 0x8000000;
constexpr int device_range_stop = 0x10000000;

class arch_pt_element {
public:
    arch_pt_element() = delete;

    static inline bool user(pt_element *e) { return e->x & (1 << 6); } // AP[1]
    static inline bool accessed(pt_element *e) { return e->x & (1 << 10); } // AF

    static inline void set_user(pt_element *e, bool v) { e->set_bit(6, v); } // AP[1]
    static inline void set_accessed(pt_element *e, bool v) { e->set_bit(10, v); } // AF

    /* false->non-shareable true->Inner Shareable */
    static inline void set_share(pt_element *e, bool v) {
        e->x &= ~(3ul << 8);
        if (v)
            e->x |= (3ul << 8);
    }

    static inline void set_attridx(pt_element *e, unsigned char c) {
        assert(c <= 7);
        e->x &= ~(7ul << 2);
        e->x |= (c << 2);
    }
};

/* common interface implementation */

inline bool pt_element::empty() const { return !x; }
inline bool pt_element::valid() const { return x & 0x1; }
inline bool pt_element::writable() const { return !(x & (1ul << 7)); } // AP[2]
inline bool pt_element::executable() const { return !(x & (1ul << 53)); } // Priv. Execute Never
inline bool pt_element::dirty() const { return x & (1ul << 55); } // Software Use[0]
inline bool pt_element::large() const { return (x & 0x3) == 0x1; }

inline bool pt_element::sw_bit(unsigned off) const {
    assert(off < 3);
    return (x >> (56 + off)) & 1;
}

inline phys pt_element::addr(bool large) const {
    u64 v = x & ((1ul << max_phys_addr_size) - 1);
    if (large)
        v &= ~0x1ffffful;
    else
        v &= ~0xffful;
    return v;
}

inline u64 pt_element::pfn(bool large) const { return addr(large) >> page_size_shift; }
inline phys pt_element::next_pt_addr() const { return addr(false); }
inline u64 pt_element::next_pt_pfn() const { return pfn(false); }

inline void pt_element::set_valid(bool v) { set_bit(0, v); }
inline void pt_element::set_writable(bool v) { set_bit(7, !v); } // AP[2]
inline void pt_element::set_executable(bool v) { set_bit(53, !v); } // Priv. Execute Never
inline void pt_element::set_dirty(bool v) { set_bit(55, v); }
inline void pt_element::set_large(bool v) { set_bit(1, !v); }

inline void pt_element::set_sw_bit(unsigned off, bool v) {
    assert(off < 3);
    set_bit(56 + off, v);
}

inline void pt_element::set_addr(phys addr, bool large) {
    u64 mask = large ? 0xffff0000001ffffful : 0xffff000000000ffful;
    x = (x & mask) | (addr & ~mask);
    x |= large ? 1 : 3;
}

inline void pt_element::set_pfn(u64 pfn, bool large) {
    set_addr(pfn << page_size_shift, large);
}

}

#endif /* ARCH_MMU_HH */
