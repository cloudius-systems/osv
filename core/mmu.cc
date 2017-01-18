/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include "processor.hh"
#include <osv/debug.hh>
#include "exceptions.hh"
#include <boost/format.hpp>
#include <string.h>
#include <iterator>
#include "libc/signal.hh"
#include <osv/align.hh>
#include <osv/ilog2.hh>
#include <osv/prio.hh>
#include <safe-ptr.hh>
#include "fs/vfs/vfs.h"
#include <osv/error.h>
#include <osv/trace.hh>
#include <stack>
#include "java/jvm/jvm_balloon.hh"
#include <fs/fs.hh>
#include <osv/file.h>
#include "dump.hh"
#include <osv/rcu.hh>
#include <osv/rwlock.h>
#include <numeric>

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

// protects vma list and page table modifications.
// anything that may add, remove, split vma, zaps pte or changes pte permission
// should hold the lock for write
rwlock_t vma_list_mutex;

// A mutex serializing modifications to the high part of the page table
// (linear map, etc.) which are not part of vma_list.
mutex page_table_high_mutex;

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
    return reinterpret_cast<uintptr_t>(virt) & (mem_area_size - 1);
}

template <int N, typename MakePTE>
phys allocate_intermediate_level(MakePTE make_pte)
{
    phys pt_page = virt_to_phys(memory::alloc_page());
    // since the pt is not yet mapped, we don't need to use hw_ptep
    pt_element<N>* pt = phys_cast<pt_element<N>>(pt_page);
    for (auto i = 0; i < pte_per_page; ++i) {
        pt[i] = make_pte(i);
    }
    return pt_page;
}

template<int N>
void allocate_intermediate_level(hw_ptep<N> ptep, pt_element<N> org)
{
    phys pt_page = allocate_intermediate_level<N>([org](int i) {
        auto tmp = org;
        phys addend = phys(i) << page_size_shift;
        tmp.set_addr(tmp.addr() | addend, false);
        return tmp;
    });
    ptep.write(make_intermediate_pte(ptep, pt_page));
}

template<int N>
void allocate_intermediate_level(hw_ptep<N> ptep)
{
    phys pt_page = allocate_intermediate_level<N>([](int i) {
        return make_empty_pte<N>();
    });
    if (!ptep.compare_exchange(make_empty_pte<N>(), make_intermediate_pte(ptep, pt_page))) {
        memory::free_page(phys_to_virt(pt_page));
    }
}

// only 4k can be cow for now
pt_element<0> pte_mark_cow(pt_element<0> pte, bool cow)
{
    if (cow) {
        pte.set_writable(false);
    }
    pte.set_sw_bit(pte_cow, cow);
    return pte;
}

template<int N>
bool change_perm(hw_ptep<N> ptep, unsigned int perm)
{
    static_assert(pt_level_traits<N>::leaf_capable::value, "non leaf pte");
    pt_element<N> pte = ptep.read();
    unsigned int old = (pte.valid() ? perm_read : 0) |
        (pte.writable() ? perm_write : 0) |
        (pte.executable() ? perm_exec : 0);

    if (pte_is_cow(pte)) {
        perm &= ~perm_write;
    }

    // Note: in x86, if the present bit (0x1) is off, not only read is
    // disallowed, but also write and exec. So in mprotect, if any
    // permission is requested, we must also grant read permission.
    // Linux does this too.
    pte.set_valid(true);
    pte.set_writable(perm & perm_write);
    pte.set_executable(perm & perm_exec);
    pte.set_rsvd_bit(0, !perm);
    ptep.write(pte);

    return old & ~perm;
}

template<int N>
void split_large_page(hw_ptep<N> ptep)
{
}

template<>
void split_large_page(hw_ptep<1> ptep)
{
    pt_element<1> pte_orig = ptep.read();
    pte_orig.set_large(false);
    allocate_intermediate_level(ptep, pte_orig);
}

struct page_allocator {
    virtual bool map(uintptr_t offset, hw_ptep<0> ptep, pt_element<0> pte, bool write) = 0;
    virtual bool map(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write) = 0;
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<0> ptep) = 0;
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<1> ptep) = 0;
    virtual ~page_allocator() {}
};

unsigned long all_vmas_size()
{
    SCOPE_LOCK(vma_list_mutex.for_read());
    return std::accumulate(vma_list.begin(), vma_list.end(), size_t(0), [](size_t s, vma& v) { return s + v.size(); });
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

enum class allocate_intermediate_opt : bool {no = true, yes = false};
enum class skip_empty_opt : bool {no = true, yes = false};
enum class descend_opt : bool {no = true, yes = false};
enum class once_opt : bool {no = true, yes = false};
enum class split_opt : bool {no = true, yes = false};
enum class account_opt: bool {no = true, yes = false};

// Parameter descriptions:
//  Allocate - if "yes" page walker will allocate intermediate page if one is missing
//             otherwise it will skip to next address.
//  Skip     - if "yes" page walker will not call leaf page handler on an empty pte.
//  Descend  - if "yes" page walker will descend one level if large page range is mapped
//             by small pages, otherwise it will call huge_page() on intermediate small pte
//  Once     - if "yes" page walker will not loop over range of pages
//  Split    - If "yes" page walker will split huge pages to small pages while walking
template<allocate_intermediate_opt Allocate, skip_empty_opt Skip = skip_empty_opt::yes,
        descend_opt Descend = descend_opt::yes, once_opt Once = once_opt::no, split_opt Split = split_opt::yes>
class page_table_operation {
protected:
    template<typename T>  bool opt2bool(T v) { return v == T::yes; }
public:
    bool allocate_intermediate(void) { return opt2bool(Allocate); }
    bool skip_empty(void) { return opt2bool(Skip); }
    bool descend(void) { return opt2bool(Descend); }
    bool once(void) { return opt2bool(Once); }
    template<int N>
    bool split_large(hw_ptep<N> ptep, int level) { return opt2bool(Split); }
    unsigned nr_page_sizes(void) { return mmu::nr_page_sizes; }

    template<int N>
    pt_element<N> ptep_read(hw_ptep<N> ptep) { return ptep.read(); }

    // page() function is called on leaf ptes. Each page table operation
    // have to provide its own version.
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) { assert(0); }
    // if huge page range is covered by smaller pages some page table operations
    // may want to have special handling for level 1 non leaf pte. intermediate_page_pre()
    // is called just before descending into the next level, while intermediate_page_post()
    // is called just after.
    void intermediate_page_pre(hw_ptep<1> ptep, uintptr_t offset) {}
    void intermediate_page_post(hw_ptep<1> ptep, uintptr_t offset) {}
    // Page walker calls page() when it a whole leaf page need to be handled, but if it
    // has 2M pte and less then 2M of virt memory to operate upon and split is disabled
    // sup_page is called instead. So if you are here it means that page walker encountered
    // 2M pte and page table operation wants to do something special with sub-region of it
    // since it disabled splitting.
    void sub_page(hw_ptep<1> ptep, int level, uintptr_t offset) { return; }
};

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::large_capable::value>::type
sub_page(PageOps& pops, hw_ptep<N> ptep, int level, uintptr_t offset)
{
    pops.sub_page(ptep, level, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::large_capable::value>::type
sub_page(PageOps& pops, hw_ptep<N> ptep, int level, uintptr_t offset)
{
}

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::leaf_capable::value, bool>::type
page(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    return pops.page(ptep, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::leaf_capable::value, bool>::type
page(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    assert(0);
    return false;
}

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::large_capable::value>::type
intermediate_page_pre(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    pops.intermediate_page_pre(ptep, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::large_capable::value>::type
intermediate_page_pre(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
}

template<typename PageOps, int N>
static inline typename std::enable_if<pt_level_traits<N>::large_capable::value>::type
intermediate_page_post(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
    pops.intermediate_page_post(ptep, offset);
}

template<typename PageOps, int N>
static inline typename std::enable_if<!pt_level_traits<N>::large_capable::value>::type
intermediate_page_post(PageOps& pops, hw_ptep<N> ptep, uintptr_t offset)
{
}

template<typename PageOp, int ParentLevel> class map_level;

template<typename PageOp>
        void map_range(uintptr_t vma_start, uintptr_t vstart, size_t size, PageOp& page_mapper, size_t slop = page_size)
{
    map_level<PageOp, 4> pt_mapper(vma_start, vstart, size, page_mapper, slop);
    pt_mapper(hw_ptep<4>::force(mmu::get_root_pt(vstart)));
}

template<typename PageOp, int ParentLevel> class map_level {
private:
    uintptr_t vma_start;
    uintptr_t vcur;
    uintptr_t vend;
    size_t slop;
    PageOp& page_mapper;
    static constexpr int level = ParentLevel - 1;

    friend void map_range<PageOp>(uintptr_t, uintptr_t, size_t, PageOp&, size_t);
    friend class map_level<PageOp, ParentLevel + 1>;

    map_level(uintptr_t vma_start, uintptr_t vcur, size_t size, PageOp& page_mapper, size_t slop) :
        vma_start(vma_start), vcur(vcur), vend(vcur + size - 1), slop(slop), page_mapper(page_mapper) {}
    pt_element<ParentLevel> read(const hw_ptep<ParentLevel>& ptep) const {
        return page_mapper.ptep_read(ptep);
    }
    pt_element<level> read(const hw_ptep<level>& ptep) const {
        return page_mapper.ptep_read(ptep);
    }
    hw_ptep<level> follow(hw_ptep<ParentLevel> ptep)
    {
        return hw_ptep<level>::force(phys_cast<pt_element<level>>(read(ptep).next_pt_addr()));
    }
    bool skip_pte(hw_ptep<level> ptep) {
        return page_mapper.skip_empty() && read(ptep).empty();
    }
    bool descend(hw_ptep<level> ptep) {
        return page_mapper.descend() && !read(ptep).empty() && !read(ptep).large();
    }
    template<int N>
    typename std::enable_if<N == 0>::type
    map_range(uintptr_t vcur, size_t size, PageOp& page_mapper, size_t slop,
            hw_ptep<N> ptep, uintptr_t base_virt)
    {
    }
    template<int N>
    typename std::enable_if<N == level && N != 0>::type
    map_range(uintptr_t vcur, size_t size, PageOp& page_mapper, size_t slop,
            hw_ptep<N> ptep, uintptr_t base_virt)
    {
        map_level<PageOp, level> pt_mapper(vma_start, vcur, size, page_mapper, slop);
        pt_mapper(ptep, base_virt);
    }
    void operator()(hw_ptep<ParentLevel> parent, uintptr_t base_virt = 0) {
        if (!read(parent).valid()) {
            if (!page_mapper.allocate_intermediate()) {
                return;
            }
            allocate_intermediate_level(parent);
        } else if (read(parent).large()) {
            if (page_mapper.split_large(parent, ParentLevel)) {
                // We're trying to change a small page out of a huge page (or
                // in the future, potentially also 2 MB page out of a 1 GB),
                // so we need to first split the large page into smaller pages.
                // Our implementation ensures that it is ok to free pieces of a
                // alloc_huge_page() with free_page(), so it is safe to do such a
                // split.
                split_large_page(parent);
            } else {
                // If page_mapper does not want to split, let it handle subpage by itself
                sub_page(page_mapper, parent, ParentLevel, base_virt - vma_start);
                return;
            }
        }
        auto pt = follow(parent);
        phys step = phys(1) << (page_size_shift + level * pte_per_page_shift);
        auto idx = pt_index(vcur, level);
        auto eidx = pt_index(vend, level);
        base_virt += idx * step;
        base_virt = (int64_t(base_virt) << 16) >> 16; // extend 47th bit

        do {
            auto ptep = pt.at(idx);
            uintptr_t vstart1 = vcur, vend1 = vend;
            clamp(vstart1, vend1, base_virt, base_virt + step - 1, slop);
            if (unsigned(level) < page_mapper.nr_page_sizes() && vstart1 == base_virt && vend1 == base_virt + step - 1) {
                uintptr_t offset = base_virt - vma_start;
                if (level) {
                    if (!skip_pte(ptep)) {
                        if (descend(ptep) || !page(page_mapper, ptep, offset)) {
                            intermediate_page_pre(page_mapper, ptep, offset);
                            map_range(vstart1, vend1 - vstart1 + 1, page_mapper, slop, ptep, base_virt);
                            intermediate_page_post(page_mapper, ptep, offset);
                        }
                    }
                } else {
                    if (!skip_pte(ptep)) {
                        page(page_mapper, ptep, offset);
                    }
                }
            } else {
                map_range(vstart1, vend1 - vstart1 + 1, page_mapper, slop, ptep, base_virt);
            }
            base_virt += step;
            ++idx;
        } while(!page_mapper.once() && idx <= eidx);
    }
};

class linear_page_mapper :
        public page_table_operation<allocate_intermediate_opt::yes, skip_empty_opt::no, descend_opt::no> {
    phys start;
    phys end;
    mattr mem_attr;
public:
    linear_page_mapper(phys start, size_t size, mattr mem_attr = mattr_default) :
        start(start), end(start + size), mem_attr(mem_attr) {}
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        phys addr = start + offset;
        assert(addr < end);
        ptep.write(make_leaf_pte(ptep, addr, mmu::perm_rwx, mem_attr));
        return true;
    }
};

template<allocate_intermediate_opt Allocate, skip_empty_opt Skip = skip_empty_opt::yes,
         account_opt Account = account_opt::no>
class vma_operation :
        public page_table_operation<Allocate, Skip, descend_opt::yes, once_opt::no, split_opt::yes> {
public:
    // returns true if tlb flush is needed after address range processing is completed.
    bool tlb_flush_needed(void) { return false; }
    // this function is called at the very end of operate_range(). vma_operation may do
    // whatever cleanup is needed here.
    void finalize(void) { return; }

    ulong account_results(void) { return _total_operated; }
    void account(size_t size) { if (this->opt2bool(Account)) _total_operated += size; }
private:
    // We don't need locking because each walk will create its own instance, so
    // while two instances can operate over the same linear address (therefore
    // all the cmpxcghs), the same instance will go linearly over its duty.
    ulong _total_operated = 0;
};

/*
 * populate() populates the page table with the entries it is (assumed to be)
 * missing to span the given virtual-memory address range, and then pre-fills
 * (using the given fill function) these pages and sets their permissions to
 * the given ones. This is part of the mmap implementation.
 */
template <account_opt T = account_opt::no>
class populate : public vma_operation<allocate_intermediate_opt::yes, skip_empty_opt::no, T> {
private:
    page_allocator* _page_provider;
    unsigned int _perm;
    bool _write;
    bool _map_dirty;
    template<int N>
    bool skip(pt_element<N> pte) {
        if (pte.empty()) {
            return false;
        }
        return !_write || pte.writable();
    }
    template<int N>
    inline pt_element<N> dirty(pt_element<N> pte) {
        pte.set_dirty(_map_dirty || _write);
        return pte;
    }
public:
    populate(page_allocator* pops, unsigned int perm, bool write = false, bool map_dirty = true) :
        _page_provider(pops), _perm(perm), _write(write), _map_dirty(map_dirty) { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        auto pte = ptep.read();
        if (skip(pte)) {
            return true;
        }

        pte = dirty(make_leaf_pte(ptep, 0, _perm));

        try {
            if (_page_provider->map(offset, ptep, pte, _write)) {
                this->account(pt_level_traits<N>::size::value);
            }
        } catch(std::exception&) {
            return false;
        }
        return true;
    }
};

template <account_opt Account = account_opt::no>
class populate_small : public populate<Account> {
public:
    populate_small(page_allocator* pops, unsigned int perm, bool write = false, bool map_dirty = true) :
        populate<Account>(pops, perm, write, map_dirty) { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        assert(!pt_level_traits<N>::large_capable::value);
        return populate<Account>::page(ptep, offset);
    }
    unsigned nr_page_sizes(void) { return 1; }
};

class splithugepages : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes, account_opt::no> {
public:
    splithugepages() { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset)
    {
        assert(!pt_level_traits<N>::large_capable::value);
        return true;
    }
    unsigned nr_page_sizes(void) { return 1; }
};

struct tlb_gather {
    static constexpr size_t max_pages = 20;
    struct tlb_page {
        void* addr;
        size_t size;
    };
    size_t nr_pages = 0;
    tlb_page pages[max_pages];
    bool push(void* addr, size_t size) {
        bool flushed = false;
        if (nr_pages == max_pages) {
            flush();
            flushed = true;
        }
        pages[nr_pages++] = { addr, size };
        return flushed;
    }
    bool flush() {
        if (!nr_pages) {
            return false;
        }
        mmu::flush_tlb_all();
        for (auto i = 0u; i < nr_pages; ++i) {
            auto&& tp = pages[i];
            if (tp.size == page_size) {
                memory::free_page(tp.addr);
            } else {
                memory::free_huge_page(tp.addr, tp.size);
            }
        }
        nr_pages = 0;
        return true;
    }
};

/*
 * Undo the operation of populate(), freeing memory allocated by populate()
 * and marking the pages non-present.
 */
template <account_opt T = account_opt::no>
class unpopulate : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes, T> {
private:
    tlb_gather _tlb_gather;
    page_allocator* _pops;
    bool do_flush = false;
public:
    unpopulate(page_allocator* pops) : _pops(pops) {}
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        void* addr = phys_to_virt(ptep.read().addr());
        size_t size = pt_level_traits<N>::size::value;
        // Note: we free the page even if it is already marked "not present".
        // evacuate() makes sure we are only called for allocated pages, and
        // not-present may only mean mprotect(PROT_NONE).
        if (_pops->unmap(addr, offset, ptep)) {
            do_flush = !_tlb_gather.push(addr, size);
        } else {
            do_flush = true;
        }
        this->account(size);
        return true;
    }
    void intermediate_page_post(hw_ptep<1> ptep, uintptr_t offset) {
        osv::rcu_defer([](void *page) { memory::free_page(page); }, phys_to_virt(ptep.read().addr()));
        ptep.write(make_empty_pte<1>());
    }
    bool tlb_flush_needed(void) {
        return !_tlb_gather.flush() && do_flush;
    }
    void finalize(void) {}
};

class protection : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes> {
private:
    unsigned int perm;
    bool do_flush;
public:
    protection(unsigned int perm) : perm(perm), do_flush(false) { }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        do_flush |= change_perm(ptep, perm);
        return true;
    }
    bool tlb_flush_needed(void) {return do_flush;}
};

template <typename T, account_opt Account = account_opt::no>
class dirty_cleaner : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes, Account> {
private:
    bool do_flush;
    T handler;
public:
    dirty_cleaner(T handler) : do_flush(false), handler(handler) {}

    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        pt_element<N> pte = ptep.read();
        if (!pte.dirty()) {
            return true;
        }
        do_flush |= true;
        pte.set_dirty(false);
        ptep.write(pte);
        handler(ptep.read().addr(), offset, pt_level_traits<N>::size::value);
        return true;
    }

    bool tlb_flush_needed(void) {return do_flush;}
    void finalize() {
        handler.finalize();
    }
};

class dirty_page_sync {
    friend dirty_cleaner<dirty_page_sync, account_opt::yes>;
    friend file_vma;
private:
    file *_file;
    f_offset _offset;
    uint64_t _size;
    struct elm {
        iovec iov;
        off_t offset;
    };
    std::stack<elm> queue;
    dirty_page_sync(file *file, f_offset offset, uint64_t size) : _file(file), _offset(offset), _size(size) {}
    void operator()(phys addr, uintptr_t offset, size_t size) {
        off_t off = _offset + offset;
        size_t len = std::min(size, _size - off);
        queue.push(elm{{phys_to_virt(addr), len}, off});
    }
    void finalize() {
        while(!queue.empty()) {
            elm w = queue.top();
            uio data{&w.iov, 1, w.offset, ssize_t(w.iov.iov_len), UIO_WRITE};
            int error = _file->write(&data, FOF_OFFSET);
            if (error) {
                throw make_error(error);
            }
            queue.pop();
        }
    }
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
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        assert(result == null);
        result = ptep.read().addr() | (v & ~pte_level_mask(N));
        return true;
    }
    void sub_page(hw_ptep<1> ptep, int l, uintptr_t offset) {
        assert(ptep.read().large());
        page(ptep, offset);
    }
};

class cleanup_intermediate_pages
    : public page_table_operation<
          allocate_intermediate_opt::no,
          skip_empty_opt::yes,
          descend_opt::yes,
          once_opt::no,
          split_opt::no> {
public:
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        if (!pt_level_traits<N>::large_capable::value) {
            ++live_ptes;
        }
        return true;
    }
    void intermediate_page_pre(hw_ptep<1> ptep, uintptr_t offset) {
        live_ptes = 0;
    }
    void intermediate_page_post(hw_ptep<1> ptep, uintptr_t offset) {
        if (!live_ptes) {
            auto old = ptep.read();
            auto v = phys_cast<u64*>(old.addr());
            for (unsigned i = 0; i < 512; ++i) {
                assert(v[i] == 0);
            }
            ptep.write(make_empty_pte<1>());
            osv::rcu_defer([](void *page) { memory::free_page(page); }, phys_to_virt(old.addr()));
            do_flush = true;
        }
    }
    bool tlb_flush_needed() { return do_flush; }
    void finalize() {}
    ulong account_results(void) { return 0; }
private:
    unsigned live_ptes;
    bool do_flush = false;
};

class virt_to_pte_map_rcu :
        public page_table_operation<allocate_intermediate_opt::no, skip_empty_opt::yes,
        descend_opt::yes, once_opt::yes, split_opt::no> {
private:
    virt_pte_visitor& _visitor;
    virt_to_pte_map_rcu(virt_pte_visitor& visitor) : _visitor(visitor) {}

public:
    friend void virt_visit_pte_rcu(uintptr_t, virt_pte_visitor&);
    template<int N>
    pt_element<N> ptep_read(hw_ptep<N> ptep) {
        return ptep.ll_read();
    }
    template<int N>
    bool page(hw_ptep<N> ptep, uintptr_t offset) {
        auto pte = ptep_read(ptep);
        _visitor.pte(pte);
        assert(pt_level_traits<N>::large_capable::value == pte.large());
        return true;
    }
    void sub_page(hw_ptep<1> ptep, int l, uintptr_t offset) {
        page(ptep, offset);
    }
};

template<typename T> ulong operate_range(T mapper, void *vma_start, void *start, size_t size)
{
    start = align_down(start, page_size);
    size = std::max(align_up(size, page_size), page_size);
    uintptr_t virt = reinterpret_cast<uintptr_t>(start);
    map_range(reinterpret_cast<uintptr_t>(vma_start), virt, size, mapper);

    // TODO: consider if instead of requesting a full TLB flush, we should
    // instead try to make more judicious use of INVLPG - e.g., in
    // split_large_page() and other specific places where we modify specific
    // page table entries.
    if (mapper.tlb_flush_needed()) {
        mmu::flush_tlb_all();
    }
    mapper.finalize();
    return mapper.account_results();
}

template<typename T> ulong operate_range(T mapper, void *start, size_t size)
{
    return operate_range(mapper, start, start, size);
}

phys virt_to_phys_pt(void* virt)
{
    auto v = reinterpret_cast<uintptr_t>(virt);
    auto vbase = align_down(v, page_size);
    virt_to_phys_map v2p_mapper(v);
    map_range(vbase, vbase, page_size, v2p_mapper);
    return v2p_mapper.addr();
}

void virt_visit_pte_rcu(uintptr_t virt, virt_pte_visitor& visitor)
{
    auto vbase = align_down(virt, page_size);
    virt_to_pte_map_rcu v2pte_mapper(visitor);
    WITH_LOCK(osv::rcu_read_lock) {
        map_range(vbase, vbase, page_size, v2pte_mapper);
    }
}

bool contains(uintptr_t start, uintptr_t end, vma& y)
{
    return y.start() >= start && y.end() <= end;
}

// So that we don't need to create a vma (with size, permission and alot of
// other irrelevant data) just to find an address in the vma list, we have
// the following addr_compare, which compares exactly like vma_compare does,
// except that it takes a bare uintptr_t instead of a vma.
class addr_compare {
public:
    bool operator()(const vma& x, uintptr_t y) const { return x.start() < y; }
    bool operator()(uintptr_t x, const vma& y) const { return x < y.start(); }
};

// Find the single (if any) vma which contains the given address.
// The complexity is logarithmic in the number of vmas in vma_list.
static inline vma_list_type::iterator
find_intersecting_vma(uintptr_t addr) {
    auto vma = vma_list.lower_bound(addr, addr_compare());
    if (vma->start() == addr) {
        return vma;
    }
    // Otherwise, vma->start() > addr, so we need to check the previous vma
    --vma;
    if (addr >= vma->start() && addr < vma->end()) {
        return vma;
    } else {
        return vma_list.end();
    }
}

// Find the list of vmas which intersect a given address range. Because the
// vmas are sorted in vma_list, the result is a consecutive slice of vma_list,
// [first, second), between the first returned iterator (inclusive), and the
// second returned iterator (not inclusive).
// The complexity is logarithmic in the number of vmas in vma_list.
static inline std::pair<vma_list_type::iterator, vma_list_type::iterator>
find_intersecting_vmas(const addr_range& r)
{
    if (r.end() <= r.start()) { // empty range, so nothing matches
        return {vma_list.end(), vma_list.end()};
    }
    auto start = vma_list.lower_bound(r.start(), addr_compare());
    if (start->start() > r.start()) {
        // The previous vma might also intersect with our range if it ends
        // after our range's start.
        auto prev = std::prev(start);
        if (prev->end() > r.start()) {
            start = prev;
        }
    }
    // If the start vma is actually beyond the end of the search range,
    // there is no intersection.
    if (start->start() >= r.end()) {
        return {vma_list.end(), vma_list.end()};
    }
    // end is the first vma starting >= r.end(), so any previous vma (after
    // start) surely started < r.end() so is part of the intersection.
    auto end = vma_list.lower_bound(r.end(), addr_compare());
    return {start, end};
}


/**
 * Change virtual memory range protection
 *
 * Change protection for a virtual memory range.  Updates page tables and VMas
 * for populated memory regions and just VMAs for unpopulated ranges.
 *
 * \return returns EACCESS/EPERM if requested permission cannot be granted
 */
static error protect(const void *addr, size_t size, unsigned int perm)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t end = start + size;
    auto range = find_intersecting_vmas(addr_range(start, end));
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
            i->operate_range(protection(perm));
        }
    }
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
    throw make_error(ENOMEM);
}

ulong evacuate(uintptr_t start, uintptr_t end)
{
    auto range = find_intersecting_vmas(addr_range(start, end));
    ulong ret = 0;
    for (auto i = range.first; i != range.second; ++i) {
        i->split(end);
        i->split(start);
        if (contains(start, end, *i)) {
            auto& dead = *i--;
            auto size = dead.operate_range(unpopulate<account_opt::yes>(dead.page_ops()));
            ret += size;
            if (dead.has_flags(mmap_jvm_heap)) {
                memory::stats::on_jvm_heap_free(size);
            }
            vma_list.erase(dead);
            delete &dead;
        }
    }
    return ret;
    // FIXME: range also indicates where we can insert a new anon_vma, use it
}

static void unmap(const void* addr, size_t size)
{
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    evacuate(start, start+size);
}

static error sync(const void* addr, size_t length, int flags)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto end = start+length;
    auto err = make_error(ENOMEM);
    auto range = find_intersecting_vmas(addr_range(start, end));
    for (auto i = range.first; i != range.second; ++i) {
        err = i->sync(std::max(start, i->start()), std::min(end, i->end()));
        if (err.bad()) {
            break;
        }
    }
    return err;
}

class uninitialized_anonymous_page_provider : public page_allocator {
private:
    virtual void* fill(void* addr, uint64_t offset, uintptr_t size) {
        return addr;
    }
    template<int N>
    bool set_pte(void *addr, hw_ptep<N> ptep, pt_element<N> pte) {
        if (!addr) {
            throw std::exception();
        }
        if (!write_pte(addr, ptep, make_empty_pte<N>(), pte)) {
            if (pt_level_traits<N>::large_capable::value) {
                memory::free_huge_page(addr, pt_level_traits<N>::size::value);
            } else {
                memory::free_page(addr);
            }
            return false;
        }
        return true;
    }
public:
    virtual bool map(uintptr_t offset, hw_ptep<0> ptep, pt_element<0> pte, bool write) override {
        return set_pte(fill(memory::alloc_page(), offset, page_size), ptep, pte);
    }
    virtual bool map(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write) override {
        size_t size = pt_level_traits<1>::size::value;
        return set_pte(fill(memory::alloc_huge_page(size), offset, size), ptep, pte);
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<0> ptep) override {
        clear_pte(ptep);
        return true;
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<1> ptep) override {
        clear_pte(ptep);
        return true;
    }
};

class initialized_anonymous_page_provider : public uninitialized_anonymous_page_provider {
private:
    virtual void* fill(void* addr, uint64_t offset, uintptr_t size) override {
        if (addr) {
            memset(addr, 0, size);
        }
        return addr;
    }
};

class map_file_page_read : public uninitialized_anonymous_page_provider {
private:
    file *_file;
    f_offset foffset;

    virtual void* fill(void* addr, uint64_t offset, uintptr_t size) override {
        if (addr) {
            iovec iovec {addr, size};
            uio data {&iovec, 1, off_t(foffset + offset), ssize_t(size), UIO_READ};
            _file->read(&data, FOF_OFFSET);
            /* zero buffer tail on a short read */
            if (data.uio_resid) {
                size_t tail = std::min(size, size_t(data.uio_resid));
                memset((char*)addr + size - tail, 0, tail);
            }
        }
        return addr;
    }
public:
    map_file_page_read(file *file, f_offset foffset) :
        _file(file), foffset(foffset) {}
    virtual ~map_file_page_read() {};
};

class map_file_page_mmap : public page_allocator {
private:
    file* _file;
    off_t _foffset;
    bool _shared;

public:
    map_file_page_mmap(file *file, off_t off, bool shared) : _file(file), _foffset(off), _shared(shared) {}
    virtual ~map_file_page_mmap() {};

    virtual bool map(uintptr_t offset, hw_ptep<0> ptep,  pt_element<0> pte, bool write) override {
        return _file->map_page(offset + _foffset, ptep, pte, write, _shared);
    }
    virtual bool map(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write) override {
        return _file->map_page(offset + _foffset, ptep, pte, write, _shared);
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<0> ptep) override {
        return _file->put_page(addr, offset + _foffset, ptep);
    }
    virtual bool unmap(void *addr, uintptr_t offset, hw_ptep<1> ptep) override {
        return _file->put_page(addr, offset + _foffset, ptep);
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
    } else {
        // we don't know if the given range is free, need to evacuate it first
        evacuate(start, start+size);
    }
    v->set(start, start+size);

    vma_list.insert(*v);

    return start;
}

inline bool in_vma_range(void* addr)
{
    return reinterpret_cast<long>(addr) >= 0;
}

void vpopulate(void* addr, size_t size)
{
    assert(!in_vma_range(addr));
    WITH_LOCK(page_table_high_mutex) {
        initialized_anonymous_page_provider map;
        operate_range(populate<>(&map, perm_rwx), addr, size);
    }
}

void vdepopulate(void* addr, size_t size)
{
    assert(!in_vma_range(addr));
    WITH_LOCK(page_table_high_mutex) {
        initialized_anonymous_page_provider map;
        operate_range(unpopulate<>(&map), addr, size);
    }
}

void vcleanup(void* addr, size_t size)
{
    assert(!in_vma_range(addr));
    WITH_LOCK(page_table_high_mutex) {
        cleanup_intermediate_pages cleaner;
        operate_range(cleaner, addr, addr, size);
    }
}

static void depopulate(void* addr, size_t length)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto range = find_intersecting_vmas(addr_range(start, start + length));
    for (auto i = range.first; i != range.second; ++i) {
        i->operate_range(unpopulate<>(i->page_ops()), reinterpret_cast<void*>(start), std::min(length, i->size()));
        start += i->size();
        length -= i->size();
    }
}

static void nohugepage(void* addr, size_t length)
{
    length = align_up(length, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto range = find_intersecting_vmas(addr_range(start, start + length));
    for (auto i = range.first; i != range.second; ++i) {
        if (!i->has_flags(mmap_small)) {
            i->update_flags(mmap_small);
            i->operate_range(splithugepages(), reinterpret_cast<void*>(start), std::min(length, i->size()));
        }
        start += i->size();
        length -= i->size();
    }
}

error advise(void* addr, size_t size, int advice)
{
    WITH_LOCK(vma_list_mutex.for_write()) {
        if (!ismapped(addr, size)) {
            return make_error(ENOMEM);
        }
        if (advice == advise_dontneed) {
            depopulate(addr, size);
            return no_error();
        } else if (advice == advise_nohugepage) {
            nohugepage(addr, size);
            return no_error();
        }
        return make_error(EINVAL);
    }
}

template<account_opt Account = account_opt::no>
ulong populate_vma(vma *vma, void *v, size_t size, bool write = false)
{
    page_allocator *map = vma->page_ops();
    auto total = vma->has_flags(mmap_small) ?
        vma->operate_range(populate_small<Account>(map, vma->perm(), write, vma->map_dirty()), v, size) :
        vma->operate_range(populate<Account>(map, vma->perm(), write, vma->map_dirty()), v, size);

    return total;
}

void* map_anon(const void* addr, size_t size, unsigned flags, unsigned perm)
{
    bool search = !(flags & mmap_fixed);
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto* vma = new mmu::anon_vma(addr_range(start, start + size), perm, flags);
    SCOPE_LOCK(vma_list_mutex.for_write());
    auto v = (void*) allocate(vma, start, size, search);
    if (flags & mmap_populate) {
        populate_vma(vma, v, size);
    }
    return v;
}

std::unique_ptr<file_vma> default_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return std::unique_ptr<file_vma>(new file_vma(range, perm, flags, file, offset, new map_file_page_read(file, offset)));
}

std::unique_ptr<file_vma> map_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return std::unique_ptr<file_vma>(new file_vma(range, perm, flags, file, offset, new map_file_page_mmap(file, offset, flags & mmap_shared)));
}

void* map_file(const void* addr, size_t size, unsigned flags, unsigned perm,
              fileref f, f_offset offset)
{
    bool search = !(flags & mmu::mmap_fixed);
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto *vma = f->mmap(addr_range(start, start + size), flags | mmap_file, perm, offset).release();
    void *v;
    WITH_LOCK(vma_list_mutex.for_write()) {
        v = (void*) allocate(vma, start, size, search);
        if (flags & mmap_populate) {
            populate_vma(vma, v, std::min(size, align_up(::size(f), page_size)));
        }
    }
    return v;
}

bool is_linear_mapped(const void *addr, size_t size)
{
    if ((addr >= elf_start) && (addr + size <= elf_start + elf_size)) {
        return true;
    }
    return addr >= phys_mem;
}

// Checks if the entire given memory region is mmap()ed (in vma_list).
bool ismapped(const void *addr, size_t size)
{
    uintptr_t start = (uintptr_t) addr;
    uintptr_t end = start + size;

    auto range = find_intersecting_vmas(addr_range(start, end));
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

bool access_fault(vma& vma, unsigned int error_code)
{
    auto perm = vma.perm();
    if (mmu::is_page_fault_insn(error_code)) {
        return !(perm & perm_exec);
    }

    if (mmu::is_page_fault_write(error_code)) {
        return !(perm & perm_write);
    }

    return !(perm & perm_read);
}

TRACEPOINT(trace_mmu_vm_fault, "addr=%p, error_code=%x", uintptr_t, unsigned int);
TRACEPOINT(trace_mmu_vm_fault_sigsegv, "addr=%p, error_code=%x, %s", uintptr_t, unsigned int, const char*);
TRACEPOINT(trace_mmu_vm_fault_ret, "addr=%p, error_code=%x", uintptr_t, unsigned int);

static void vm_sigsegv(uintptr_t addr, exception_frame* ef)
{
    void *pc = ef->get_pc();
    if (pc >= text_start && pc < text_end) {
        debug_ll("page fault outside application, addr: 0x%016lx\n", addr);
        dump_registers(ef);
        abort();
    }
    osv::handle_mmap_fault(addr, SIGSEGV, ef);
}

static void vm_sigbus(uintptr_t addr, exception_frame* ef)
{
    osv::handle_mmap_fault(addr, SIGBUS, ef);
}

void vm_fault(uintptr_t addr, exception_frame* ef)
{
    trace_mmu_vm_fault(addr, ef->get_error());
    if (fast_sigsegv_check(addr, ef)) {
        vm_sigsegv(addr, ef);
        trace_mmu_vm_fault_sigsegv(addr, ef->get_error(), "fast");
        return;
    }
    addr = align_down(addr, mmu::page_size);
    WITH_LOCK(vma_list_mutex.for_read()) {
        auto vma = find_intersecting_vma(addr);
        if (vma == vma_list.end() || access_fault(*vma, ef->get_error())) {
            vm_sigsegv(addr, ef);
            trace_mmu_vm_fault_sigsegv(addr, ef->get_error(), "slow");
            return;
        }
        vma->fault(addr, ef);
    }
    trace_mmu_vm_fault_ret(addr, ef->get_error());
}

vma::vma(addr_range range, unsigned perm, unsigned flags, bool map_dirty, page_allocator *page_ops)
    : _range(align_down(range.start(), mmu::page_size), align_up(range.end(), mmu::page_size))
    , _perm(perm)
    , _flags(flags)
    , _map_dirty(map_dirty)
    , _page_ops(page_ops)
{
}

vma::~vma()
{
}

void vma::set(uintptr_t start, uintptr_t end)
{
    _range = addr_range(align_down(start, mmu::page_size), align_up(end, mmu::page_size));
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

unsigned vma::flags() const
{
    return _flags;
}

void vma::update_flags(unsigned flag)
{
    assert(vma_list_mutex.wowned());
    _flags |= flag;
}

bool vma::has_flags(unsigned flag)
{
    return _flags & flag;
}

template<typename T> ulong vma::operate_range(T mapper, void *addr, size_t size)
{
    return mmu::operate_range(mapper, reinterpret_cast<void*>(start()), addr, size);
}

template<typename T> ulong vma::operate_range(T mapper)
{
    void *addr = reinterpret_cast<void*>(start());
    return mmu::operate_range(mapper, addr, addr, size());
}

bool vma::map_dirty()
{
    return _map_dirty;
}

void vma::fault(uintptr_t addr, exception_frame *ef)
{
    auto hp_start = align_up(_range.start(), huge_page_size);
    auto hp_end = align_down(_range.end(), huge_page_size);
    size_t size;
    if (!has_flags(mmap_jvm_balloon|mmap_small) && (hp_start <= addr && addr < hp_end)) {
        addr = align_down(addr, huge_page_size);
        size = huge_page_size;
    } else {
        size = page_size;
    }

    auto total = populate_vma<account_opt::yes>(this, (void*)addr, size,
        mmu::is_page_fault_write(ef->get_error()));

    if (_flags & mmap_jvm_heap) {
        memory::stats::on_jvm_heap_alloc(total);
    }
}

page_allocator* vma::page_ops()
{
    return _page_ops;
}

static uninitialized_anonymous_page_provider page_allocator_noinit;
static initialized_anonymous_page_provider page_allocator_init;
static page_allocator *page_allocator_noinitp = &page_allocator_noinit, *page_allocator_initp = &page_allocator_init;

anon_vma::anon_vma(addr_range range, unsigned perm, unsigned flags)
    : vma(range, perm, flags, true, (flags & mmap_uninitialized) ? page_allocator_noinitp : page_allocator_initp)
{
}

void anon_vma::split(uintptr_t edge)
{
    if (edge <= _range.start() || edge >= _range.end()) {
        return;
    }
    vma* n = new anon_vma(addr_range(edge, _range.end()), _perm, _flags);
    set(_range.start(), edge);
    vma_list.insert(*n);
}

error anon_vma::sync(uintptr_t start, uintptr_t end)
{
    return no_error();
}

// Balloon is backed by no pages, but in the case of partial copy, we may have
// to back some of the pages. For that and for that only, we initialize a page
// allocator. It is fine in this case to use the noinit allocator. Since this
// area was supposed to be holding the balloon object before, so the JVM will
// not count on it being initialized to any value.
jvm_balloon_vma::jvm_balloon_vma(unsigned char *jvm_addr, uintptr_t start,
                                 uintptr_t end, balloon_ptr b, unsigned perm, unsigned flags)
    : vma(addr_range(start, end), perm_rw, flags | mmap_jvm_balloon, true, page_allocator_noinitp),
      _balloon(b), _jvm_addr(jvm_addr),
      _real_perm(perm), _real_flags(flags & ~mmap_jvm_balloon), _real_size(end - start)
{
}

// IMPORTANT: This code assumes that opportunistic copying never happens during
// partial copying.  In general, this assumption is wrong. There is nothing
// that prevents the JVM from doing both at the same time from the same object.
// However, hotspot seems not to do it, which simplifies a lot our code.
//
// If that assumption fails to hold in some real life scenario (be it a hotspot
// corner case or another JVM), the assertion eff == _effective_jvm_addr will
// crash us and we will find it out.  If we need it, it is not impossible to
// handle this case: all we have to do is create a list of effective addresses
// and keep the partial counts independently.
//
// Explanation about partial copy:
//
// There are situations during which some Garbage Collectors will copy a large
// object in parallel, using various threads, each being responsible for a part
// of the object.
//
// If that happens, the simple balloon move algorithm will break. However,
// because offset 'x' in the source will always be copied to offset 'x' in the
// destination, we can still calculate the final destination object. This
// address is the _effective_jvm_addr in the code below.
//
// The problem is that we cannot open the new balloon yet. Since the JVM
// believes it is copying only a part of the object, the destination may (and
// usually will) contain valid objects, that need to be themselves moved
// somewhere else before we can install our object there.
//
// Also, we can't close the object fully when someone writes to it: because a
// part of the object is now already freed, the JVM may and will go ahead and
// copy another object to this location. To handle this case, we use the
// variable _partial_copy, which keeps track of how much data has being copied
// from this location to somewhere else. Because we know that the JVM has to
// copy the whole object, when that counter reaches the amount of bytes we
// expect in this vma, this means we can close this object (assuming no
// opportunistic copy)
//
// It is also possible that the region will be written to during partial copy.
// Although it is invalid to overwrite pieces of the object, it is perfectly
// valid to write to locations that were already copied from. This is handled
// in the fault handler itself, by mapping pages to the location that currently
// holds the balloon vma. At some point, we will create an anonymous vma in its
// place.
bool jvm_balloon_vma::add_partial(size_t partial, unsigned char *eff)
{
    if (_effective_jvm_addr) {
        assert(eff == _effective_jvm_addr);
    } else {
        _effective_jvm_addr= eff;
    }

    _partial_copy += partial;
    return _partial_copy == real_size();
}

void jvm_balloon_vma::split(uintptr_t edge)
{
    abort();
}

error jvm_balloon_vma::sync(uintptr_t start, uintptr_t end)
{
    return no_error();
}

void jvm_balloon_vma::fault(uintptr_t fault_addr, exception_frame *ef)
{
    if (jvm_balloon_fault(_balloon, ef, this)) {
        return;
    }
    // Can only reach this case if we are doing partial copies
    assert(_effective_jvm_addr);
    // FIXME : This will always use a small page, due to the flag check we have
    // in vma::fault. We can try to map the original worker with a huge page,
    // and try to see if we succeed. Using a huge page is harder than it seems,
    // because the JVM is not guaranteed to copy objects in huge page
    // increments - and it usually won't.  If we go ahead and map a huge page
    // subsequent copies *from* this location will not fault and we will lose
    // track of the partial copy count.
    vma::fault(fault_addr, ef);
}

jvm_balloon_vma::~jvm_balloon_vma()
{
    // it believes the objects are no longer valid. It could be the case
    // for a dangling mapping representing a balloon that was already moved
    // out.
    vma_list.erase(*this);
    assert(!(_real_flags & mmap_jvm_balloon));
    mmu::map_anon(addr(), size(), _real_flags, _real_perm);

    if (_effective_jvm_addr) {
        // Can't just use size(), because although rare, the source and destination can
        // have different alignments
        auto end = align_down(_effective_jvm_addr + balloon_size, balloon_alignment);
        auto s = end - align_up(_effective_jvm_addr, balloon_alignment);
        mmu::map_jvm(_effective_jvm_addr, s, mmu::huge_page_size, _balloon);
    }
}

ulong map_jvm(unsigned char* jvm_addr, size_t size, size_t align, balloon_ptr b)
{
    auto addr = align_up(jvm_addr, align);
    auto start = reinterpret_cast<uintptr_t>(addr);

    vma* v;
    WITH_LOCK(vma_list_mutex.for_read()) {
        u64 a = reinterpret_cast<u64>(addr);
        v = &*find_intersecting_vma(a);

        // It has to be somewhere!
        assert(v != &*vma_list.end());
        assert(v->has_flags(mmap_jvm_heap) | v->has_flags(mmap_jvm_balloon));
        if (v->has_flags(mmap_jvm_balloon) && (v->addr() == addr)) {
            jvm_balloon_vma *j = static_cast<jvm_balloon_vma *>(&*v);
            if (&*j->_balloon != &*b) {
                j->_balloon = b;
            }
            return 0;
        }
    }

    auto* vma = new mmu::jvm_balloon_vma(jvm_addr, start, start + size, b, v->perm(), v->flags());

    WITH_LOCK(vma_list_mutex.for_write()) {
        // This means that the mapping that we had before was a balloon mapping
        // that was laying around and wasn't updated to an anon mapping. If we
        // allow it to split it would significantly complicate our code, since
        // now the finishing code would have to deal with the case where the
        // bounds found in the vma are not the real bounds. We delete it right
        // away and avoid it altogether.
        auto range = find_intersecting_vmas(addr_range(start, start + size));

        for (auto i = range.first; i != range.second; ++i) {
            if (i->has_flags(mmap_jvm_balloon)) {
                jvm_balloon_vma *jvma = static_cast<jvm_balloon_vma *>(&*i);
                // If there is an effective address this means this is a
                // partial copy. We cannot close it here because the copy is
                // still ongoing. We can, though, assume that if we are
                // installing a new vma over a part of this region, that
                // particular part was already copied to in the original
                // balloon.
                //
                // FIXME: This is solvable by reducing the size of the vma and
                // keeping track of the original size. Still, we can't really
                // call the split code directly because that will delete the
                // vma and cause its termination
                if (jvma->effective_jvm_addr() != nullptr) {
                    auto end = start + size;
                    // Should have exited before the creation of the vma,
                    // just updating the balloon pointer.
                    assert(jvma->start() != start);
                    // Since we will change its position in the tree, for the sake of future
                    // lookups we need to reinsert it.
                    vma_list.erase(*jvma);
                    if (jvma->start() < start) {
                        assert(jvma->partial() >= (jvma->end() - start));
                        jvma->set(jvma->start(), start);
                    } else {
                        assert(jvma->partial() >= (end - jvma->start()));
                        jvma->set(end, jvma->end());
                    }
                    vma_list.insert(*jvma);
                } else {
                    // Note how v and jvma are different. This is because this one,
                    // we will delete.
                    auto& v = *i--;
                    vma_list.erase(v);
                    // Finish the move. In practice, it will temporarily remap an
                    // anon mapping here, but this should be rare. Let's not
                    // complicate the code to optimize it. There are no
                    // guarantees that we are talking about the same balloon If
                    // this is the old balloon
                    delete &v;
                }
            }
        }

        evacuate(start, start + size);
        vma_list.insert(*vma);
        return vma->size();
    }
    return 0;
}

file_vma::file_vma(addr_range range, unsigned perm, unsigned flags, fileref file, f_offset offset, page_allocator* page_ops)
    : vma(range, perm, flags | mmap_small, !(flags & mmap_shared), page_ops)
    , _file(file)
    , _offset(offset)
{
    int err = validate_perm(perm);

    if (err != 0) {
        throw make_error(err);
    }
}

void file_vma::fault(uintptr_t addr, exception_frame *ef)
{
    auto hp_start = align_up(_range.start(), huge_page_size);
    auto hp_end = align_down(_range.end(), huge_page_size);
    auto fsize = ::size(_file);
    if (offset(addr) >= fsize) {
        vm_sigbus(addr, ef);
        return;
    }
    size_t size;
    if (!has_flags(mmap_small) && (hp_start <= addr && addr < hp_end) && offset(hp_end) < fsize) {
        addr = align_down(addr, huge_page_size);
        size = huge_page_size;
    } else {
        size = page_size;
    }

    populate_vma<account_opt::no>(this, (void*)addr, size,
            mmu::is_page_fault_write(ef->get_error()));
}

file_vma::~file_vma()
{
    delete _page_ops;
}

void file_vma::split(uintptr_t edge)
{
    if (edge <= _range.start() || edge >= _range.end()) {
        return;
    }
    auto off = offset(edge);
    vma *n = _file->mmap(addr_range(edge, _range.end()), _flags, _perm, off).release();
    set(_range.start(), edge);
    vma_list.insert(*n);
}

error file_vma::sync(uintptr_t start, uintptr_t end)
{
    if (!has_flags(mmap_shared))
        return make_error(ENOMEM);

    // Called when ZFS arc cache is not present.
    if (_page_ops && dynamic_cast<map_file_page_read *>(_page_ops)) {
        start = std::max(start, _range.start());
        end = std::min(end, _range.end());
        uintptr_t size = end - start;

        dirty_page_sync sync(_file.get(), _offset, ::size(_file));
        error err = no_error();
        try {
            if (operate_range(dirty_cleaner<dirty_page_sync, account_opt::yes>(sync), (void*)start, size) != 0) {
                err = make_error(sys_fsync(_file.get()));
            }
        } catch (error e) {
            err = e;
        }
        return err;
    }

    try {
        _file->sync(_offset + start - _range.start(), _offset + end - _range.start());
    } catch (error& err) {
        return err;
    }

    return make_error(sys_fsync(_file.get()));
}

int file_vma::validate_perm(unsigned perm)
{
    // fail if mapping a file that is not opened for reading.
    if (!(_file->f_flags & FREAD)) {
        return EACCES;
    }
    if (perm & perm_write) {
        if (has_flags(mmap_shared) && !(_file->f_flags & FWRITE)) {
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

f_offset file_vma::offset(uintptr_t addr)
{
    return _offset + (addr - _range.start());
}

std::unique_ptr<file_vma> shm_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return map_file_mmap(this, range, flags, perm, offset);
}

void* shm_file::page(uintptr_t hp_off)
{
    void *addr;

    auto p = _pages.find(hp_off);
    if (p == _pages.end()) {
        addr = memory::alloc_huge_page(huge_page_size);
        memset(addr, 0, huge_page_size);
        _pages.emplace(hp_off, addr);
    } else {
        addr = p->second;
    }

    return addr;
}

bool shm_file::map_page(uintptr_t offset, hw_ptep<0> ptep, pt_element<0> pte, bool write, bool shared)
{
    uintptr_t hp_off = align_down(offset, huge_page_size);

    return write_pte(static_cast<char*>(page(hp_off)) + offset - hp_off, ptep, pte);
}

bool shm_file::map_page(uintptr_t offset, hw_ptep<1> ptep, pt_element<1> pte, bool write, bool shared)
{
    uintptr_t hp_off = align_down(offset, huge_page_size);

    assert(hp_off == offset);

    return write_pte(static_cast<char*>(page(hp_off)) + offset - hp_off, ptep, pte);
}

bool shm_file::put_page(void *addr, uintptr_t offset, hw_ptep<0> ptep) {return false;}
bool shm_file::put_page(void *addr, uintptr_t offset, hw_ptep<1> ptep) {return false;}

shm_file::shm_file(size_t size, int flags) : special_file(flags, DTYPE_UNSPEC), _size(size) {}

int shm_file::stat(struct stat* buf)
{
    buf->st_size = _size;
    return 0;
}

int shm_file::close()
{
    for (auto& i : _pages) {
        memory::free_huge_page(i.second, huge_page_size);
    }
    _pages.clear();
    return 0;
}

void linear_map(void* _virt, phys addr, size_t size,
                size_t slop, mattr mem_attr)
{
    uintptr_t virt = reinterpret_cast<uintptr_t>(_virt);
    slop = std::min(slop, page_size_level(nr_page_sizes - 1));
    assert((virt & (slop - 1)) == (addr & (slop - 1)));
    linear_page_mapper phys_map(addr, size, mem_attr);
    map_range(virt, virt, size, phys_map, slop);
}

void free_initial_memory_range(uintptr_t addr, size_t size)
{
    memory::free_initial_memory_range(phys_cast<void>(addr), size);
}

error mprotect(const void *addr, size_t len, unsigned perm)
{
    SCOPE_LOCK(vma_list_mutex.for_write());

    if (!ismapped(addr, len)) {
        return make_error(ENOMEM);
    }

    return protect(addr, len, perm);
}

error munmap(const void *addr, size_t length)
{
    SCOPE_LOCK(vma_list_mutex.for_write());

    length = align_up(length, mmu::page_size);
    if (!ismapped(addr, length)) {
        return make_error(EINVAL);
    }
    sync(addr, length, 0);
    unmap(addr, length);
    return no_error();
}

error msync(const void* addr, size_t length, int flags)
{
    SCOPE_LOCK(vma_list_mutex.for_read());

    if (!ismapped(addr, length)) {
        return make_error(ENOMEM);
    }
    return sync(addr, length, flags);
}

error mincore(const void *addr, size_t length, unsigned char *vec)
{
    char *end = align_up((char *)addr + length, page_size);
    char tmp;
    SCOPE_LOCK(vma_list_mutex.for_read());
    if (!is_linear_mapped(addr, length) && !ismapped(addr, length)) {
        return make_error(ENOMEM);
    }
    for (char *p = (char *)addr; p < end; p += page_size) {
        if (safe_load(p, tmp)) {
            *vec++ = 0x01;
        } else {
            *vec++ = 0x00;
        }
    }
    return no_error();
}

std::string procfs_maps()
{
    std::ostringstream os;
    WITH_LOCK(vma_list_mutex.for_read()) {
        for (auto& vma : vma_list) {
            char read    = vma.perm() & perm_read  ? 'r' : '-';
            char write   = vma.perm() & perm_write ? 'w' : '-';
            char execute = vma.perm() & perm_exec  ? 'x' : '-';
            char priv    = 'p';
            osv::fprintf(os, "%x-%x %c%c%c%c ", vma.start(), vma.end(), read, write, execute, priv);
            if (vma.flags() & mmap_file) {
                const file_vma &f_vma = static_cast<file_vma&>(vma);
                osv::fprintf(os, "%08x 00:00 0 %s\n", f_vma.offset(), f_vma.file()->f_dentry->d_path);
            } else {
                osv::fprintf(os, "00000000 00:00 0\n");
            }
        }
    }
    return os.str();
}

}
