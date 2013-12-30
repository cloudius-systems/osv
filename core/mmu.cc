/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "mmu.hh"
#include "mempool.hh"
#include "processor.hh"
#include "debug.hh"
#include "exceptions.hh"
#include <boost/format.hpp>
#include <string.h>
#include <iterator>
#include "libc/signal.hh"
#include "align.hh"
#include "interrupt.hh"
#include "ilog2.hh"
#include "prio.hh"
#include <safe-ptr.hh>
#include "fs/vfs/vfs.h"
#include <osv/error.h>
#include <osv/trace.hh>
#include "arch-mmu.hh"
#include <stack>

extern void* elf_start;
extern size_t elf_size;

namespace {

typedef boost::format fmt;

}

extern const char text_start[], text_end[];

namespace mmu {

namespace bi = boost::intrusive;

class vma_compare {
public:
    bool operator ()(const vma& a, const vma& b) const {
        return a.addr() < b.addr();
    }
};

typedef boost::intrusive::set<vma,
                              bi::compare<vma_compare>,
                              bi::member_hook<vma,
                                              bi::set_member_hook<>,
                                              &vma::_vma_list_hook>,
                              bi::optimize_size<true>
                              > vma_list_base;

struct vma_list_type : vma_list_base {
    vma_list_type() {
        // insert markers for the edges of allocatable area
        // simplifies searches
        insert(*new anon_vma(addr_range(0, 0), 0, 0));
        uintptr_t e = 0x800000000000;
        insert(*new anon_vma(addr_range(e, e), 0, 0));
    }
};

__attribute__((init_priority((int)init_prio::vma_list)))
vma_list_type vma_list;

// A fairly coarse-grained mutex serializing modifications to both
// vma_list and the page table itself.
mutex vma_list_mutex;

hw_ptep follow(pt_element pte)
{
    return hw_ptep::force(phys_cast<pt_element>(pte.next_pt_addr()));
}

// 1's for the bits provided by the pte for this level
// 0's for the bits provided by the virtual address for this level
phys pte_level_mask(unsigned level)
{
    auto shift = level * ilog2_roundup_constexpr(pte_per_page)
        + ilog2_roundup_constexpr(page_size);
    return ~((phys(1) << shift) - 1);
}

void* phys_to_virt(phys pa)
{
    // The ELF is mapped 1:1
    void* phys_addr = reinterpret_cast<void*>(pa);
    if ((phys_addr >= elf_start) && (phys_addr < elf_start + elf_size)) {
        return phys_addr;
    }

    return phys_mem + pa;
}

phys virt_to_phys_pt(void* virt);

phys virt_to_phys(void *virt)
{
    // The ELF is mapped 1:1
    if ((virt >= elf_start) && (virt < elf_start + elf_size)) {
        return reinterpret_cast<phys>(virt);
    }

#if CONF_debug_memory
    if (virt > debug_base) {
        return virt_to_phys_pt(virt);
    }
#endif

    // For now, only allow non-mmaped areas.  Later, we can either
    // bounce such addresses, or lock them in memory and translate
    assert(virt >= phys_mem);
    return static_cast<char*>(virt) - phys_mem;
}

phys allocate_intermediate_level()
{
    phys pt_page = virt_to_phys(memory::alloc_page());
    // since the pt is not yet mapped, we don't need to use hw_ptep
    pt_element* pt = phys_cast<pt_element>(pt_page);
    for (auto i = 0; i < pte_per_page; ++i) {
        pt[i] = make_empty_pte();
    }
    return pt_page;
}

void allocate_intermediate_level(hw_ptep ptep)
{
    phys pt_page = allocate_intermediate_level();
    ptep.write(make_normal_pte(pt_page));
}

bool change_perm(hw_ptep ptep, unsigned int perm)
{
    pt_element pte = ptep.read();
    unsigned int old = (pte.present() ? perm_read : 0) |
            (pte.writable() ? perm_write : 0) |
            (!pte.nx() ? perm_exec : 0);
    // Note: in x86, if the present bit (0x1) is off, not only read is
    // disallowed, but also write and exec. So in mprotect, if any
    // permission is requested, we must also grant read permission.
    // Linux does this too.
    pte.set_present(perm);
    pte.set_writable(perm & perm_write);
    pte.set_nx(!(perm & perm_exec));
    ptep.write(pte);

    return old & ~perm;
}

void split_large_page(hw_ptep ptep, unsigned level)
{
    pt_element pte_orig = ptep.read();
    if (level == 1) {
        pte_orig.set_large(false);
    }
    allocate_intermediate_level(ptep);
    auto pt = follow(ptep.read());
    for (auto i = 0; i < pte_per_page; ++i) {
        pt_element tmp = pte_orig;
        phys addend = phys(i) << (page_size_shift + pte_per_page_shift * (level - 1));
        tmp.set_addr(tmp.addr(level > 1) | addend, level > 1);
        pt.at(i).write(tmp);
    }
}

struct fill_page {
public:
    virtual void fill(void* addr, uint64_t offset, uintptr_t size) = 0;
};

void debug_count_ptes(pt_element pte, int level, size_t &nsmall, size_t &nhuge)
{
    if (level<4 && !pte.present()){
        // nothing
    } else if (pte.large()) {
        nhuge++;
    } else if (level==0){
        nsmall++;
    } else {
        hw_ptep pt = follow(pte);
        for(int i=0; i<pte_per_page; ++i) {
            debug_count_ptes(pt.at(i).read(), level-1, nsmall, nhuge);
        }
    }
}


void tlb_flush_this_processor()
{
    // TODO: we can use page_table_root instead of read_cr3(), can be faster
    // when shadow page tables are used.
    processor::write_cr3(processor::read_cr3());
}

// tlb_flush() does TLB flush on *all* processors, not returning before all
// processors confirm flushing their TLB. This is slow, but necessary for
// correctness so that, for example, after mprotect() returns, no thread on
// no cpu can write to the protected page.
mutex tlb_flush_mutex;
sched::thread *tlb_flush_waiter;
std::atomic<int> tlb_flush_pendingconfirms;

inter_processor_interrupt tlb_flush_ipi{[] {
        tlb_flush_this_processor();
        if (tlb_flush_pendingconfirms.fetch_add(-1) == 1) {
            tlb_flush_waiter->wake();
        }
}};

void tlb_flush()
{
    tlb_flush_this_processor();
    if (sched::cpus.size() <= 1)
        return;
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_waiter = sched::thread::current();
    tlb_flush_pendingconfirms.store((int)sched::cpus.size() - 1);
    tlb_flush_ipi.send_allbutself();
    sched::thread::wait_until([] {
            return tlb_flush_pendingconfirms.load() == 0;
    });
}

void clamp(uintptr_t& vstart1, uintptr_t& vend1,
           uintptr_t min, size_t max, size_t slop)
{
    vstart1 &= ~(slop - 1);
    vend1 |= (slop - 1);
    vstart1 = std::max(vstart1, min);
    vend1 = std::min(vend1, max);
}

constexpr unsigned pt_index(uintptr_t virt, unsigned level)
{
    return pt_index(reinterpret_cast<void*>(virt), level);
}

unsigned nr_page_sizes = 2; // FIXME: detect 1GB pages

void set_nr_page_sizes(unsigned nr)
{
    nr_page_sizes = nr;
}

pt_element page_table_root;

constexpr size_t page_size_level(unsigned level)
{
    return size_t(1) << (page_size_shift + pte_per_page_shift * level);
}

enum class allocate_intermediate_opt : bool {no = true, yes = false};
enum class skip_empty_opt : bool {no = true, yes = false};
enum class descend_opt : bool {no = true, yes = false};
enum class once_opt : bool {no = true, yes = false};
enum class split_opt : bool {no = true, yes = false};

// Parameter descriptions:
//  Allocate - if "yes" page walker will allocate intermediate page if one is missing
//             otherwise it will skip to next address.
//  Skip     - if "yes" page walker will not call leaf page handler (small_page/huge_page)
//             on an empty pte.
//  Descend  - if "yes" page walker will descend one level if large page range is mapped
//             by small pages, otherwise it will call huge_page() on intermediate small pte
//  Once     - if "yes" page walker will not loop over range of pages
//  Split    - If "yes" page walker will split huge pages to small pages while walking
template<allocate_intermediate_opt Allocate, skip_empty_opt Skip = skip_empty_opt::yes,
        descend_opt Descend = descend_opt::yes, once_opt Once = once_opt::no, split_opt Split = split_opt::yes>
class page_table_operation {
private:
    template<typename T>  bool opt2bool(T v) { return v == T::yes; }
public:
    bool allocate_intermediate(void) { return opt2bool(Allocate); }
    bool skip_empty(void) { return opt2bool(Skip); }
    bool descend(void) { return opt2bool(Descend); }
    bool once(void) { return opt2bool(Once); }
    bool split_large(hw_ptep ptep, int level) { return opt2bool(Split); }

    // small_page() function is called on level 0 ptes. Each page table operation
    // have to provide its own version.
    void small_page(hw_ptep ptep, uintptr_t offset) { assert(0); }
    // huge_page() function is called on leaf pte with level > 0 (currently only level 1,
    // but may handle level 2 in the feature too). Each page table operation
    // have to provide its own version.
    void huge_page(hw_ptep ptep, uintptr_t offset) { assert(0); }
    // if huge page range is covered by smaller pages some page table operations
    // may want to have special handling for level 1 non leaf pte. intermediate_page_post()
    // is called on such page after small_page() is called on all leaf pages in range
    void intermediate_page_post(hw_ptep ptep, uintptr_t offset) { return; }
    // Page walker calls small_page() when it has 4K region of virtual memory to
    // deal with and huge_page() when it has 2M of virtual memory, but if it has
    // 2M pte less then 2M of virt memory to operate upon and split is disabled
    // neither of those two will be called, sup_page will be called instead. So
    // if you are here it means that page walker encountered 2M pte and page table
    // operation wants to do something special with sub-region of it since it disabled
    // splitting.
    void sub_page(hw_ptep ptep, int level, uintptr_t offset) { return; }
};

template<typename PageOp, int ParentLevel> class map_level;
template<typename PageOp> struct map_level<PageOp, -1>
{
    map_level(uintptr_t vstart, size_t size, PageOp page_mapper, size_t slop) {}
    void operator()(hw_ptep parent, uintptr_t base_virt, uintptr_t offset) {
        assert(0);
    }
};

typedef std::integral_constant<int, 4> max_level;

template<typename PageOp, typename ParentLevel = max_level>
        void map_range(uintptr_t vstart, size_t size, PageOp& page_mapper, size_t slop = page_size,
        hw_ptep ptep = hw_ptep::force(&page_table_root), ParentLevel level = max_level(),
        uintptr_t base_virt = 0, uintptr_t offset = 0)
{
    map_level<PageOp, ParentLevel::value> pt_mapper(vstart, size, page_mapper, slop);
    pt_mapper(ptep, base_virt, offset);
}

template<typename PageOp, int ParentLevel = 4> class map_level {
private:
    uintptr_t vstart;
    uintptr_t vend;
    size_t slop;
    PageOp& page_mapper;
    typedef std::integral_constant<int, ParentLevel - 1> level;

    bool skip_pte(hw_ptep ptep) {
        return page_mapper.skip_empty() && ptep.read().empty();
    }
    bool descend(hw_ptep ptep) {
        return page_mapper.descend() && !ptep.read().empty() && !ptep.read().large();
    }
public:
    map_level(uintptr_t vstart, size_t size, PageOp& page_mapper, size_t slop = page_size) :
        vstart(vstart), vend(vstart + size - 1), slop(slop), page_mapper(page_mapper) {}
    void operator()(hw_ptep parent, uintptr_t base_virt = 0, uintptr_t offset = 0) {
        if (!parent.read().present()) {
            if (!page_mapper.allocate_intermediate()) {
                return;
            }
            allocate_intermediate_level(parent);
        } else if (parent.read().large()) {
            if (ParentLevel > 0 && page_mapper.split_large(parent, ParentLevel)) {
                // We're trying to change a small page out of a huge page (or
                // in the future, potentially also 2 MB page out of a 1 GB),
                // so we need to first split the large page into smaller pages.
                // Our implementation ensures that it is ok to free pieces of a
                // alloc_huge_page() with free_page(), so it is safe to do such a
                // split.
                split_large_page(parent, ParentLevel);
            } else {
                // If page_mapper does not want to split, let it handle subpage by itself
                page_mapper.sub_page(parent, ParentLevel, offset);
                return;
            }
        }
        hw_ptep pt = follow(parent.read());
        phys step = phys(1) << (page_size_shift + level::value * pte_per_page_shift);
        auto idx = pt_index(vstart, level::value);
        auto eidx = pt_index(vend, level::value);
        base_virt += idx * step;
        base_virt = (int64_t(base_virt) << 16) >> 16; // extend 47th bit

        do {
            hw_ptep ptep = pt.at(idx);
            uintptr_t vstart1 = vstart, vend1 = vend;
            clamp(vstart1, vend1, base_virt, base_virt + step - 1, slop);
            if (level::value < nr_page_sizes && vstart1 == base_virt && vend1 == base_virt + step - 1) {
                if (level::value) {
                    if (!skip_pte(ptep)) {
                        if (descend(ptep) || !page_mapper.huge_page(ptep, offset)) {
                            map_range(vstart1, vend1 - vstart1 + 1, page_mapper, slop, ptep,
                                    level(), base_virt, offset);
                            page_mapper.intermediate_page_post(ptep, offset);
                        }
                    }
                } else {
                    if (!skip_pte(ptep)) {
                        page_mapper.small_page(ptep, offset);
                    }
                }
            } else {
                map_range(vstart1, vend1 - vstart1 + 1, page_mapper, slop, ptep,
                        level(), base_virt, offset);
            }
            base_virt += step;
            offset += step;
            ++idx;
        } while(!page_mapper.once() && idx <= eidx);
    }
};

class linear_page_mapper :
        public page_table_operation<allocate_intermediate_opt::yes, skip_empty_opt::no, descend_opt::no> {
    phys start;
    phys end;
public:
    linear_page_mapper(phys start, size_t size) : start(start), end(start + size) {}
    void small_page(hw_ptep ptep, uintptr_t offset) {
        phys addr = start + offset;
        assert(addr < end);
        ptep.write(make_normal_pte(addr));
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset) {
        phys addr = start + offset;
        assert(addr < end);
        ptep.write(make_large_pte(addr));
        return true;
    }
};

template<allocate_intermediate_opt Allocate, skip_empty_opt Skip = skip_empty_opt::yes>
class vma_operation :
        public page_table_operation<Allocate, Skip, descend_opt::yes, once_opt::no, split_opt::yes> {
public:
    // returns true if tlb flush is needed after address range processing is completed.
    bool tlb_flush_needed(void) { return false; }
    // this function is called at the very end of operate_range(). vma_operation may do
    // whatever cleanup is needed here.
    void finalize(void) { return; }
};

/*
 * populate() populates the page table with the entries it is (assumed to be)
 * missing to span the given virtual-memory address range, and then pre-fills
 * (using the given fill function) these pages and sets their permissions to
 * the given ones. This is part of the mmap implementation.
 */
class populate : public vma_operation<allocate_intermediate_opt::yes, skip_empty_opt::no> {
private:
    fill_page *fill;
    unsigned int perm;
public:
    populate(fill_page *fill, unsigned int perm) : fill(fill), perm(perm) { }
    void small_page(hw_ptep ptep, uintptr_t offset){
        if (!ptep.read().empty()) {
            return;
        }
        phys page = virt_to_phys(memory::alloc_page());
        fill->fill(phys_to_virt(page), offset, page_size);
        if (!ptep.compare_exchange(make_empty_pte(), make_normal_pte(page, perm))) {
            memory::free_page(phys_to_virt(page));
        }
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset){
        auto pte = ptep.read();
        if (!pte.empty()) {
            return true;
        }
        void *vpage = memory::alloc_huge_page(huge_page_size);
        if (!vpage) {
            return false;
        }

        phys page = virt_to_phys(vpage);
        fill->fill(vpage, offset, huge_page_size);
        if (!ptep.compare_exchange(make_empty_pte(), make_large_pte(page, perm))) {
            memory::free_huge_page(phys_to_virt(page), huge_page_size);
        }
        return true;
    }
};

/*
 * Undo the operation of populate(), freeing memory allocated by populate()
 * and marking the pages non-present.
 */
class unpopulate : public vma_operation<allocate_intermediate_opt::no> {
private:
    std::stack<void*> small_pages;
    std::stack<void*> huge_pages;
public:
    void small_page(hw_ptep ptep, uintptr_t offset) {
        // Note: we free the page even if it is already marked "not present".
        // evacuate() makes sure we are only called for allocated pages, and
        // not-present may only mean mprotect(PROT_NONE).
        pt_element pte = ptep.read();
        ptep.write(make_empty_pte());
        small_pages.push(phys_to_virt(pte.addr(false)));
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset) {
        pt_element pte = ptep.read();
        ptep.write(make_empty_pte());
        huge_pages.push(phys_to_virt(pte.addr(false)));
        return true;
    }
    void intermediate_page(hw_ptep ptep, uintptr_t offset) {
        small_page(ptep, offset);
    }
    bool tlb_flush_needed(void) {
        return !small_pages.empty() || !huge_pages.empty();
    }
    void finalize(void) {
        while (!small_pages.empty()) {
            memory::free_page(small_pages.top());
            small_pages.pop();
        }
        while (!huge_pages.empty()) {
            memory::free_huge_page(huge_pages.top(), huge_page_size);
            huge_pages.pop();
        }
    }
};

class protection : public vma_operation<allocate_intermediate_opt::no> {
private:
    unsigned int perm;
    bool do_flush;
public:
    protection(unsigned int perm) : perm(perm), do_flush(false) { }
    void small_page(hw_ptep ptep, uintptr_t offset) {
        do_flush |= change_perm(ptep, perm);
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset) {
        do_flush |= change_perm(ptep, perm);
        return true;
    }
    bool tlb_flush_needed(void) {return do_flush;}
};

class virt_to_phys_map :
        public page_table_operation<allocate_intermediate_opt::no, skip_empty_opt::yes,
        descend_opt::yes, once_opt::yes, split_opt::no> {
private:
    uintptr_t v;
    phys result;
    static constexpr phys null = ~0ull;
    virt_to_phys_map(uintptr_t v) : v(v), result(null) {}

    phys addr(void) {
        assert(result != null);
        return result;
    }
public:
    friend phys virt_to_phys_pt(void* virt);
    void small_page(hw_ptep ptep, uintptr_t offset) {
        assert(result == null);
        result = ptep.read().addr(false) | (v & ~pte_level_mask(0));
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset) {
        assert(result == null);
        result = ptep.read().addr(true) | (v & ~pte_level_mask(1));
        return true;
    }
    void sub_page(hw_ptep ptep, int l, uintptr_t offset) {
        assert(ptep.read().large());
        huge_page(ptep, offset);
    }
};

template<typename T> void operate_range(T mapper, void *start, size_t size)
{
    start = align_down(start, page_size);
    size = std::max(align_up(size, page_size), page_size);
    uintptr_t virt = reinterpret_cast<uintptr_t>(start);
    map_range(virt, size, mapper);

    // TODO: consider if instead of requesting a full TLB flush, we should
    // instead try to make more judicious use of INVLPG - e.g., in
    // split_large_page() and other specific places where we modify specific
    // page table entries.
    if (mapper.tlb_flush_needed()) {
        tlb_flush();
    }
    mapper.finalize();
}

template<typename T> void operate_range(T mapper, const vma &vma)
{
    operate_range(mapper, (void*)vma.start(), vma.size());
}

phys virt_to_phys_pt(void* virt)
{
    auto v = reinterpret_cast<uintptr_t>(virt);
    auto vbase = align_down(v, page_size);
    virt_to_phys_map v2p_mapper(v);
    map_range(vbase, page_size, v2p_mapper);
    return v2p_mapper.addr();
}

bool contains(uintptr_t start, uintptr_t end, vma& y)
{
    return y.start() >= start && y.end() <= end;
}

/**
 * Change virtual memory range protection
 *
 * Change protection for a virtual memory range.  Updates page tables and VMas
 * for populated memory regions and just VMAs for unpopulated ranges.
 *
 * \return returns EACCESS/EPERM if requested permission cannot be granted
 */
error protect(void *addr, size_t size, unsigned int perm)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t end = start + size;
    addr_range r(start, end);
    std::lock_guard<mutex> guard(vma_list_mutex);
    auto range = vma_list.equal_range(r, vma::addr_compare());
    for (auto i = range.first; i != range.second; ++i) {
        if (i->perm() == perm)
            continue;
        int err = i->validate_perm(perm);
        if (err != 0) {
            return make_error(err);
        }
        i->split(end);
        i->split(start);
        if (contains(start, end, *i)) {
            i->protect(perm);
        }
    }
    operate_range(protection(perm), addr, size);
    return no_error();
}

uintptr_t find_hole(uintptr_t start, uintptr_t size)
{
    bool small = size < huge_page_size;
    uintptr_t good_enough = 0;

    // FIXME: use lower_bound or something
    auto p = vma_list.begin();
    auto n = std::next(p);
    while (n != vma_list.end()) {
        if (start >= p->end() && start + size <= n->start()) {
            return start;
        }
        if (p->end() >= start && n->start() - p->end() >= size) {
            good_enough = p->end();
            if (small) {
                return good_enough;
            }
            if (n->start() - align_up(good_enough, huge_page_size) >= size) {
                return align_up(good_enough, huge_page_size);
            }
        }
        p = n;
        ++n;
    }
    if (good_enough) {
        return good_enough;
    }
    abort();
}

void evacuate(uintptr_t start, uintptr_t end)
{
    addr_range r(start, end);
    std::lock_guard<mutex> guard(vma_list_mutex);
    auto range = vma_list.equal_range(r, vma::addr_compare());
    for (auto i = range.first; i != range.second; ++i) {
        i->split(end);
        i->split(start);
        if (contains(start, end, *i)) {
            auto& dead = *i--;
            operate_range(unpopulate(), dead);
            vma_list.erase(dead);
            delete &dead;
        }
    }
    // FIXME: range also indicates where we can insert a new anon_vma, use it
}

void unmap(void* addr, size_t size)
{
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    evacuate(start, start+size);
}

error msync(void* addr, size_t length, int flags)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto end = start+length;
    auto err = make_error(ENOMEM);
    addr_range r(start, end);
    WITH_LOCK(vma_list_mutex) {
        auto range = vma_list.equal_range(r, vma::addr_compare());
        for (auto i = range.first; i != range.second; ++i) {
            err = i->sync(start, end);
            if (err.bad()) {
                break;
            }
        }
    }
    return err;
}

struct fill_anon_page : fill_page {
    virtual void fill(void* addr, uint64_t offset, uintptr_t size) {
        memset(addr, 0, size);
    }
};

struct fill_anon_page_noinit: fill_page {
    virtual void fill(void* addr, uint64_t offset, uintptr_t size) {
    }
};

uintptr_t allocate(vma *v, uintptr_t start, size_t size, bool search)
{
    if (search) {
        // search for unallocated hole around start
        if (!start) {
            start = 0x200000000000ul;
        }
        start = find_hole(start, size);
        v->set(start, start+size);
    } else {
        // we don't know if the given range is free, need to evacuate it first
        evacuate(start, start+size);
    }

    vma_list.insert(*v);

    return start;
}

void vpopulate(void* addr, size_t size)
{
    fill_anon_page fill;
    WITH_LOCK(vma_list_mutex) {
        operate_range(populate(&fill, perm_rwx), addr, size);
    }
}

void vdepopulate(void* addr, size_t size)
{
    WITH_LOCK(vma_list_mutex) {
        operate_range(unpopulate(), addr, size);
    }
}

void* map_anon(void* addr, size_t size, unsigned flags, unsigned perm)
{
    bool search = !(flags & mmap_fixed);
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto* vma = new mmu::anon_vma(addr_range(start, start + size), perm, flags);
    std::lock_guard<mutex> guard(vma_list_mutex);
    auto v = (void*) allocate(vma, start, size, search);
    if (flags & mmap_populate) {
        if (flags & mmap_uninitialized) {
            fill_anon_page_noinit zfill;
            operate_range(populate(&zfill, perm), v, size);
        } else {
            fill_anon_page zfill;
            operate_range(populate(&zfill, perm), v, size);
        }
    }
    return v;
}

void* map_file(void* addr, size_t size, unsigned flags, unsigned perm,
              fileref f, f_offset offset)
{
    bool search = !(flags & mmu::mmap_fixed);
    bool shared = flags & mmu::mmap_shared;
    auto asize = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    fill_anon_page zfill;
    auto *vma = new mmu::file_vma(addr_range(start, start + size), perm, f, offset, shared);
    void *v;
    WITH_LOCK(vma_list_mutex) {
        v = (void*) allocate(vma, start, asize, search);
        operate_range(populate(&zfill, perm | perm_write), v, asize);
    }
    auto fsize = ::size(f);
    // FIXME: we pre-zeroed this, and now we're overwriting the zeroes
    if (offset < fsize) {
        read(f, v, offset, std::min(size, fsize - offset));
    }
    // FIXME: do this more cleverly, avoiding a second pass
    if (!(perm & perm_write)) {
        protect(v, asize, perm);
    }
    return v;
}

bool is_linear_mapped(void *addr, size_t size)
{
    if ((addr >= elf_start) && (addr + size <= elf_start + elf_size)) {
        return true;
    }
    return addr >= phys_mem;
}

// Checks if the entire given memory region is mmap()ed (in vma_list).
bool ismapped(void *addr, size_t size)
{
    uintptr_t start = (uintptr_t) addr;
    uintptr_t end = start + size;
    addr_range r(start, end);

    std::lock_guard<mutex> guard(vma_list_mutex);

    auto range = vma_list.equal_range(r, vma::addr_compare());
    for (auto p = range.first; p != range.second; ++p) {
        if (p->start() > start)
            return false;
        start = p->end();
        if (start >= end)
            return true;
    }
    return false;
}

// Checks if the entire given memory region is readable.
bool isreadable(void *addr, size_t size)
{
    char *end = align_up((char *)addr + size, mmu::page_size);
    char tmp;
    for (char *p = (char *)addr; p < end; p += mmu::page_size) {
        if (!safe_load(p, tmp))
            return false;
    }
    return true;
}

namespace {

uintptr_t align_down(uintptr_t ptr)
{
    return ptr & ~(page_size - 1);
}

uintptr_t align_up(uintptr_t ptr)
{
    return align_down(ptr + page_size - 1);
}

}

bool access_fault(vma& vma, unsigned long error_code)
{
    auto perm = vma.perm();
    if (error_code & page_fault_insn) {
        return true;
    }
    if (error_code & page_fault_write) {
        return !(perm & perm_write);
    }
    return !(perm & perm_read);
}

TRACEPOINT(trace_mmu_vm_fault, "addr=%p, error_code=%x", uintptr_t, u16);
TRACEPOINT(trace_mmu_vm_fault_sigsegv, "addr=%p, error_code=%x", uintptr_t, u16);
TRACEPOINT(trace_mmu_vm_fault_ret, "addr=%p, error_code=%x", uintptr_t, u16);

void vm_sigsegv(uintptr_t addr, exception_frame* ef)
{
    auto pc = reinterpret_cast<void*>(ef->rip);
    if (pc >= text_start && pc < text_end) {
        abort("page fault outside application");
    }
    osv::handle_segmentation_fault(addr, ef);
}

void vm_fault(uintptr_t addr, exception_frame* ef)
{
    trace_mmu_vm_fault(addr, ef->error_code);
    addr = align_down(addr);
    WITH_LOCK(vma_list_mutex) {
        auto vma = vma_list.find(addr_range(addr, addr+1), vma::addr_compare());
        if (vma == vma_list.end() || access_fault(*vma, ef->error_code)) {
            vm_sigsegv(addr, ef);
            trace_mmu_vm_fault_sigsegv(addr, ef->error_code);
            return;
        }
        vma->fault(addr, ef);
    }
    trace_mmu_vm_fault_ret(addr, ef->error_code);
}

vma::vma(addr_range range, unsigned perm, unsigned flags)
    : _range(align_down(range.start()), align_up(range.end()))
    , _perm(perm)
    , _flags(flags)
{
}

vma::~vma()
{
}

void vma::set(uintptr_t start, uintptr_t end)
{
    _range = addr_range(align_down(start), align_up(end));
}

void vma::protect(unsigned perm)
{
    _perm = perm;
}

uintptr_t vma::start() const
{
    return _range.start();
}

uintptr_t vma::end() const
{
    return _range.end();
}

void* vma::addr() const
{
    return reinterpret_cast<void*>(_range.start());
}

uintptr_t vma::size() const
{
    return _range.end() - _range.start();
}

unsigned vma::perm() const
{
    return _perm;
}

anon_vma::anon_vma(addr_range range, unsigned perm, unsigned flags)
    : vma(range, perm, flags)
{
}

void anon_vma::split(uintptr_t edge)
{
    if (edge <= _range.start() || edge >= _range.end()) {
        return;
    }
    vma* n = new anon_vma(addr_range(edge, _range.end()), _perm, _flags);
    _range = addr_range(_range.start(), edge);
    vma_list.insert(*n);
}

error anon_vma::sync(uintptr_t start, uintptr_t end)
{
    return no_error();
}

void anon_vma::fault(uintptr_t addr, exception_frame *ef)
{
    auto hp_start = ::align_up(_range.start(), huge_page_size);
    auto hp_end = ::align_down(_range.end(), huge_page_size);
    size_t size;
    if (hp_start <= addr && addr < hp_end) {
        addr = ::align_down(addr, huge_page_size);
        size = huge_page_size;
    } else {
        size = page_size;
    }

    if (_flags & mmap_uninitialized) {
        fill_anon_page_noinit zfill;
        operate_range(populate(&zfill, _perm), (void*)addr, size);
    } else {
        fill_anon_page zfill;
        operate_range(populate(&zfill, _perm), (void*)addr, size);
    }
}

file_vma::file_vma(addr_range range, unsigned perm, fileref file, f_offset offset, bool shared)
    : vma(range, perm, 0)
    , _file(file)
    , _offset(offset)
    , _shared(shared)
{
    int err = validate_perm(perm);

    if (err != 0) {
        throw make_error(err);
    }
}

void file_vma::split(uintptr_t edge)
{
    if (edge <= _range.start() || edge >= _range.end()) {
        return;
    }
    auto off = offset(edge);
    vma* n = new file_vma(addr_range(edge, _range.end()), _perm, _file, off, _shared);
    _range = addr_range(_range.start(), edge);
    vma_list.insert(*n);
}

error file_vma::sync(uintptr_t start, uintptr_t end)
{
    if (!_shared)
        return make_error(ENOMEM);
    start = std::max(start, _range.start());
    end = std::min(end, _range.end());
    auto fsize = ::size(_file);
    uintptr_t size = end - start;
    auto off = offset(start);
    write(_file, addr(), off, std::min(size, fsize - off));
    auto err = sys_fsync(_file.get());
    return make_error(err);
}

int file_vma::validate_perm(unsigned perm)
{
    // fail if mapping a file that is not opened for reading.
    if (!(_file->f_flags & FREAD)) {
        return EACCES;
    }
    if (perm & perm_write) {
        if (_shared && !(_file->f_flags & FWRITE)) {
            return EACCES;
        }
    }
    // fail if prot asks for PROT_EXEC and the underlying FS was
    // mounted no-exec.
    if (perm & perm_exec && (_file->f_dentry->d_mount->m_flags & MNT_NOEXEC)) {
        return EPERM;
    }
    return 0;
}

void file_vma::fault(uintptr_t addr, exception_frame *ef)
{
    abort("trying to fault-in file-backed vma");
}

f_offset file_vma::offset(uintptr_t addr)
{
    return _offset + (addr - _range.start());
}

void linear_map(void* _virt, phys addr, size_t size, size_t slop)
{
    uintptr_t virt = reinterpret_cast<uintptr_t>(_virt);
    slop = std::min(slop, page_size_level(nr_page_sizes - 1));
    assert((virt & (slop - 1)) == (addr & (slop - 1)));
    linear_page_mapper phys_map(addr, size);
    map_range(virt, size, phys_map, slop);
}

void free_initial_memory_range(uintptr_t addr, size_t size)
{
    memory::free_initial_memory_range(phys_cast<void>(addr), size);
}

void switch_to_runtime_page_table()
{
    processor::write_cr3(page_table_root.next_pt_addr());
}

}
