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
#include <osv/virt_to_phys.hh>

struct exception_frame;

struct exception_frame;

namespace mmu {

constexpr uintptr_t page_size = 4096;
constexpr int page_size_shift = 12; // log2(page_size)

constexpr int pte_per_page = 512;
constexpr int pte_per_page_shift = 9; // log2(pte_per_page)

constexpr uintptr_t huge_page_size = mmu::page_size*pte_per_page; // 2 MB

typedef uint64_t f_offset;

enum class mem_area {
    main,
    page,
    mempool,
    debug,
};

constexpr mem_area identity_mapped_areas[] = {
    mem_area::main,
    mem_area::page,
    mem_area::mempool,
};

constexpr uintptr_t mem_area_size = uintptr_t(1) << 44;

constexpr uintptr_t get_mem_area_base(mem_area area)
{
    return 0xffff800000000000 | uintptr_t(area) << 44;
}

static inline mem_area get_mem_area(void* addr)
{
    return mem_area(reinterpret_cast<uintptr_t>(addr) >> 44 & 7);
}

constexpr void* translate_mem_area(mem_area from, mem_area to, void* addr)
{
    return static_cast<void*>(static_cast<char*>(addr)
                              - get_mem_area_base(from) + get_mem_area_base(to));
}

constexpr uintptr_t main_mem_area_base = get_mem_area_base(mem_area::main);
static char* const phys_mem = reinterpret_cast<char*>(main_mem_area_base);
// area for debug allocations:
constexpr uintptr_t debug_mem_area_base = get_mem_area_base(mem_area::debug);
static char* const debug_base = reinterpret_cast<char*>(debug_mem_area_base);

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
    mmap_file        = 1ul << 7,
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

constexpr size_t page_size_level(unsigned level)
{
    return size_t(1) << (page_size_shift + pte_per_page_shift * level);
}

template<int N>
struct pt_level_traits {
    typedef typename std::integral_constant<bool, N == 0 || N == 1>::type leaf_capable;
    typedef typename std::integral_constant<bool, N == 1>::type large_capable;
    typedef typename std::integral_constant<bool, N != 0>::type intermediate_capable;
    typedef typename std::integral_constant<size_t, page_size_level(N)>::type size;
};

template<int N> class hw_ptep;
template<int N> class hw_ptep_impl;
template<int N> class hw_ptep_rcu_impl;

/* common arch-independent interface for pt_element */
template<int N>
class pt_element_common {
protected:
    explicit constexpr pt_element_common(u64 x) noexcept : x(x) {}

public:
    inline bool empty() const;
    inline bool valid() const;
    inline bool writable() const;
    inline bool executable() const;
    inline bool dirty() const;
    inline bool large() const;
    inline bool user();
    inline bool accessed();

    inline phys addr() const;
    inline u64 pfn() const;
    inline phys next_pt_addr() const;
    inline u64 next_pt_pfn() const;
    inline bool sw_bit(unsigned off) const;
    inline bool rsvd_bit(unsigned off) const;

    inline void set_valid(bool v);
    inline void set_writable(bool v);
    inline void set_executable(bool v);
    inline void set_dirty(bool v);
    inline void set_large(bool v);
    inline void set_user(bool v);
    inline void set_accessed(bool v);
    inline void set_addr(phys addr, bool large);
    inline void set_pfn(u64 pfn, bool large);
    inline void set_sw_bit(unsigned off, bool v);
    inline void set_rsvd_bit(unsigned off, bool v);

    inline void mod_addr(phys addr) {
        set_addr(addr, large());
    }
protected:
    inline void set_bit(unsigned nr, bool v) {
        x &= ~(u64(1) << nr);
        x |= u64(v) << nr;
    }
    u64 x;
    friend class hw_ptep<N>;
    friend class hw_ptep_impl<N>;
    friend class hw_ptep_rcu_impl<N>;
};
}

// arch dependent pt_element implementation
#include "arch-mmu.hh"

namespace mmu {
template<int N>
pt_element<N> make_empty_pte() { return pt_element<N>(); }

/* get the root of the page table responsible for virtual address virt */
pt_element<4> *get_root_pt(uintptr_t virt);

/* take an error code coming from the exception frame, and return
   whether the error reports a page fault (insn/write) */
bool is_page_fault_insn(unsigned int err);
bool is_page_fault_write(unsigned int err);
bool is_page_fault_write_exclusive(unsigned int err);

bool fast_sigsegv_check(uintptr_t addr, exception_frame* ef);

template<int N>
class hw_ptep_impl_base {
protected:
    hw_ptep_impl_base(pt_element<N> *ptep) : p(ptep) {}
    union {
        std::atomic<pt_element<N>>* x;
        pt_element<N>* p;
    };
};

template<int N>
class hw_ptep_impl : public hw_ptep_impl_base<N> {
public:
    pt_element<N> read() const { return *p; }
    pt_element<N> ll_read() const { return *p; }
    void write(pt_element<N> pte) {
        *const_cast<volatile u64*>(&p->x) = pte.x;
    }
protected:
    hw_ptep_impl(pt_element<N>* ptep) : hw_ptep_impl_base<N>(ptep) {}
    using hw_ptep_impl_base<N>::p;
};

template<int N>
class hw_ptep_rcu_impl : public hw_ptep_impl_base<N> {
public:
    pt_element<N> read() const { return x->load(std::memory_order_relaxed); }
    pt_element<N> ll_read() const { return x->load(std::memory_order_consume); }
    void write(pt_element<N> pte) { x->store(pte, std::memory_order_release); }

protected:
    hw_ptep_rcu_impl(pt_element<N>* ptep) : hw_ptep_impl_base<N>(ptep) {}
    using hw_ptep_impl_base<N>::x;
};

template<int N>
using hw_ptep_base = typename std::conditional<
                std::integral_constant<bool, (N == 1) || (N == 2)>::value, // only L1 and L2 PTs are RCU protected
                hw_ptep_rcu_impl<N>,
                hw_ptep_impl<N>>::type;

/* a pointer to a pte mapped by hardware.
   The arch must implement change_perm for this class. */
template <int N>
class hw_ptep : public hw_ptep_base<N> {
    static_assert(N >= 0 && N <= 4, "Wrong hw_pte level");
public:
    hw_ptep(const hw_ptep& a) : hw_ptep_base<N>(a.p) {}
    hw_ptep& operator=(const hw_ptep& a) = default;

    pt_element<N> exchange(pt_element<N> newval) {
        return x->exchange(newval);
    }
    bool compare_exchange(pt_element<N> oldval, pt_element<N> newval) {
        return x->compare_exchange_strong(oldval, newval, std::memory_order_relaxed);
    }

    hw_ptep at(unsigned idx) { return hw_ptep(p + idx); }
    static hw_ptep force(pt_element<N>* ptep) { return hw_ptep(ptep); }
    // no longer using this as a page table
    pt_element<N>* release() const { return p; }
    bool operator==(const hw_ptep& a) const noexcept { return p == a.p; }
private:
    hw_ptep(pt_element<N>* ptep) : hw_ptep_base<N>(ptep) {}
    using hw_ptep_base<N>::p;
    using hw_ptep_base<N>::x;
};

}

#endif
