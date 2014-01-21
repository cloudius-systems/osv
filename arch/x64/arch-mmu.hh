/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_MMU_HH_
#define ARCH_MMU_HH_

#include <osv/ilog2.hh>
#include <osv/mmu.hh>

namespace mmu {

class pt_element {
public:
    constexpr pt_element() : x(0) {}
    pt_element(const pt_element& a) : x(a.x) {}
    bool empty() const { return !x; }
    bool present() const { return x & 1; }
    bool writable() const { return x & 2; }
    bool user() const { return x & 4; }
    bool accessed() const { return x & 0x20; }
    bool dirty() const { return x & 0x40; }
    bool large() const { return x & 0x80; }
    bool nx() const { return x >> 63; }
    phys addr(bool large) const {
        auto v = x & ((u64(1) << (64-page_size_shift)) - 1);
        v &= ~u64(0xfff);
        v &= ~(u64(large) << page_size_shift);
        return v;
    }
    u64 pfn(bool large) const { return addr(large) >> page_size_shift; }
    phys next_pt_addr() const { return addr(false); }
    u64 next_pt_pfn() const { return pfn(false); }
    void set_present(bool v) { set_bit(0, v); }
    void set_writable(bool v) { set_bit(1, v); }
    void set_user(bool v) { set_bit(2, v); }
    void set_accessed(bool v) { set_bit(5, v); }
    void set_dirty(bool v) { set_bit(6, v); }
    void set_large(bool v) { set_bit(7, v); }
    void set_nx(bool v) { set_bit(63, v); }
    void set_addr(phys addr, bool large) {
        auto mask = 0x8000000000000fff | (u64(large) << page_size_shift);
        x = (x & mask) | addr;
    }
    void set_pfn(u64 pfn, bool large) { set_addr(pfn << page_size_shift, large); }
    static pt_element force(u64 v) { return pt_element(v); }
private:
    explicit pt_element(u64 v) : x(v) {}
    void set_bit(unsigned nr, bool v) {
        x &= ~(u64(1) << nr);
        x |= u64(v) << nr;
    }
private:
    u64 x;
    friend class hw_ptep;
};

class hw_page_table;
class hw_ptep;

// a pointer to a pte mapped by hardware.  Needed for Xen which
// makes those page tables read-only, and uses a hypercall to update
// the contents.
class hw_ptep {
public:
    hw_ptep(const hw_ptep& a) : p(a.p) {}
    pt_element read() const { return *p; }
    void write(pt_element pte) { *const_cast<volatile u64*>(&p->x) = pte.x; }
    bool compare_exchange(pt_element oldval, pt_element newval) {
        std::atomic<u64> *x = reinterpret_cast<std::atomic<u64>*>(&p->x);
        return x->compare_exchange_strong(oldval.x, newval.x, std::memory_order_relaxed);
    }
    hw_ptep at(unsigned idx) { return hw_ptep(p + idx); }
    static hw_ptep force(pt_element* ptep) { return hw_ptep(ptep); }
    // no longer using this as a page table
    pt_element* release() { return p; }
private:
    hw_ptep(pt_element* ptep) : p(ptep) {}
    pt_element* p;
    friend class hw_pte_ref;
};

inline
pt_element make_empty_pte()
{
    return pt_element();
}

inline
pt_element make_pte(phys addr, bool large,
                    unsigned perm = perm_read | perm_write | perm_exec)
{
    pt_element pte;
    pte.set_present(perm != 0);
    pte.set_writable(perm & perm_write);
    pte.set_user(true);
    pte.set_accessed(true);
    pte.set_dirty(true);
    pte.set_large(large);
    pte.set_addr(addr, large);
    pte.set_nx(!(perm & perm_exec));
    return pte;
}

inline
pt_element make_normal_pte(phys addr,
                           unsigned perm = perm_read | perm_write | perm_exec)
{
    return make_pte(addr, false, perm);
}

inline
pt_element make_large_pte(phys addr,
                          unsigned perm = perm_read | perm_write | perm_exec)
{
    return make_pte(addr, true, perm);
}

}

#endif
