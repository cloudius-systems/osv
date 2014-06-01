/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef MMU_DEFS_HH
#define MMU_DEFS_HH

#include <stdint.h>
#include <atomic>
#include <osv/rcu.hh>

struct exception_frame;

struct exception_frame;

namespace mmu {

constexpr uintptr_t page_size = 4096;
constexpr int page_size_shift = 12; // log2(page_size)

constexpr int pte_per_page = 512;
constexpr int pte_per_page_shift = 9; // log2(pte_per_page)

constexpr uintptr_t huge_page_size = mmu::page_size*pte_per_page; // 2 MB

typedef uint64_t f_offset;
typedef uint64_t phys;

static char* const phys_mem = reinterpret_cast<char*>(0xffffc00000000000);
// area for debug allocations:
static char* const debug_base = reinterpret_cast<char*>(0xffffe00000000000);

enum {
    perm_read = 1,
    perm_write = 2,
    perm_exec = 4,
    perm_rx = perm_read | perm_exec,
    perm_rw = perm_read | perm_write,
    perm_rwx = perm_read | perm_write | perm_exec,
};

enum {
    mmap_fixed       = 1ul << 0,
    mmap_populate    = 1ul << 1,
    mmap_shared      = 1ul << 2,
    mmap_uninitialized = 1ul << 3,
    mmap_jvm_heap    = 1ul << 4,
    mmap_small       = 1ul << 5,
    mmap_jvm_balloon = 1ul << 6,
};

enum {
    advise_dontneed = 1ul << 0,
    advise_nohugepage = 1ul << 1,
};

enum {
    pte_cow = 0,
};

/* flush tlb for the current processor */
void flush_tlb_local();
/* flush tlb for all */
void flush_tlb_all();

    /* static class arch_pt_element; */

/* common arch-independent interface for pt_element */
class pt_element {
public:
    constexpr pt_element() : x(0) {}
    explicit pt_element(u64 x) : x(x) {}

    inline bool empty() const;
    inline bool valid() const;
    inline bool writable() const;
    inline bool executable() const;
    inline bool dirty() const;
    inline bool large() const;
    inline phys addr(bool large) const;
    inline u64 pfn(bool large) const;
    inline phys next_pt_addr() const;
    inline u64 next_pt_pfn() const;
    inline bool sw_bit(unsigned off) const;
    inline bool rsvd_bit(unsigned off) const;

    inline void set_valid(bool v);
    inline void set_writable(bool v);
    inline void set_executable(bool v);
    inline void set_dirty(bool v);
    inline void set_large(bool v);
    inline void set_addr(phys addr, bool large);
    inline void set_pfn(u64 pfn, bool large);
    inline void set_sw_bit(unsigned off, bool v);
    inline void set_rsvd_bit(unsigned off, bool v);

    inline void mod_addr(phys addr) {
        set_addr(addr, large());
    }
private:
    inline void set_bit(unsigned nr, bool v) {
        x &= ~(u64(1) << nr);
        x |= u64(v) << nr;
    }
    u64 x;
    friend class hw_ptep;
    friend class arch_pt_element;
};

/* arch must also implement these: */
pt_element make_empty_pte();
pt_element make_normal_pte(phys addr,
                           unsigned perm = perm_read | perm_write | perm_exec);
pt_element make_large_pte(phys addr,
                          unsigned perm = perm_read | perm_write | perm_exec);

/* get the root of the page table responsible for virtual address virt */
pt_element *get_root_pt(uintptr_t virt);

/* take an error code coming from the exception frame, and return
   whether the error reports a page fault (insn/write) */
bool is_page_fault_insn(unsigned int err);
bool is_page_fault_write(unsigned int err);
bool is_page_fault_write_exclusive(unsigned int err);

bool fast_sigsegv_check(uintptr_t addr, exception_frame* ef);

/* a pointer to a pte mapped by hardware.
   The arch must implement change_perm for this class. */
class hw_ptep {
    typedef osv::rcu_ptr<pt_element> pt_ptr;
public:
    hw_ptep(const hw_ptep& a) : p(a.release()) {}
    void operator=(const hw_ptep& a) {
        p.assign(a.release());
    }
    pt_element read() const { return *release(); }
    pt_element ll_read() const { return *p.read(); }
    void write(pt_element pte) { reinterpret_cast<pt_ptr*>(release())->assign(reinterpret_cast<pt_element*>(pte.x)); }

    pt_element exchange(pt_element newval) {
        std::atomic<u64> *x = reinterpret_cast<std::atomic<u64>*>(&release()->x);
        return pt_element(x->exchange(newval.x));
    }
    bool compare_exchange(pt_element oldval, pt_element newval) {
        std::atomic<u64> *x = reinterpret_cast<std::atomic<u64>*>(&release()->x);
        return x->compare_exchange_strong(oldval.x, newval.x, std::memory_order_relaxed);
    }
    hw_ptep at(unsigned idx) { return hw_ptep(release() + idx); }
    static hw_ptep force(pt_element* ptep) { return hw_ptep(ptep); }
    // no longer using this as a page table
    pt_element* release() const { return p.read_by_owner(); }
    bool operator==(const hw_ptep& a) const noexcept { return release() == a.release(); }
private:
    hw_ptep(pt_element* ptep) : p(ptep) {}
    pt_ptr p;
};

}
#endif
