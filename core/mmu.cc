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
#include <osv/interrupt.hh>
#include <osv/ilog2.hh>
#include <osv/prio.hh>
#include <safe-ptr.hh>
#include "fs/vfs/vfs.h"
#include <osv/error.h>
#include <osv/trace.hh>
#include "arch-mmu.hh"
#include <stack>
#include "java/jvm_balloon.hh"
#include <fs/fs.hh>
#include <osv/file.h>

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

struct page_allocator {
    virtual void* alloc(uintptr_t offset) = 0;
    virtual void* alloc(size_t size, uintptr_t offset) = 0;
    virtual void free(void *addr, uintptr_t offset) = 0;
    virtual void free(void *addr, size_t size, uintptr_t offset) = 0;
    virtual void finalize() = 0;
    virtual ~page_allocator() {}
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
sched::thread_handle tlb_flush_waiter;
std::atomic<int> tlb_flush_pendingconfirms;

inter_processor_interrupt tlb_flush_ipi{[] {
        tlb_flush_this_processor();
        if (tlb_flush_pendingconfirms.fetch_add(-1) == 1) {
            tlb_flush_waiter.wake();
        }
}};

void tlb_flush()
{
    tlb_flush_this_processor();
    if (sched::cpus.size() <= 1)
        return;
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_waiter.reset(*sched::thread::current());
    tlb_flush_pendingconfirms.store((int)sched::cpus.size() - 1);
    tlb_flush_ipi.send_allbutself();
    sched::thread::wait_until([] {
            return tlb_flush_pendingconfirms.load() == 0;
    });
    tlb_flush_waiter.clear();
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
enum class account_opt: bool {no = true, yes = false};

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
protected:
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
    // may want to have special handling for level 1 non leaf pte. intermediate_page_pre()
    // is called just before descending into the next level, while intermediate_page_post()
    // is called just after.
    void intermediate_page_pre(hw_ptep ptep, uintptr_t offset) {}
    void intermediate_page_post(hw_ptep ptep, uintptr_t offset) {}
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
template<typename PageOp> class map_level<PageOp, -1>
{
private:
    friend class map_level<PageOp, 0>;
    map_level(uintptr_t vma_start, uintptr_t vcur, size_t size, PageOp page_mapper, size_t slop) {}
    void operator()(hw_ptep parent, uintptr_t base_virt) {
        assert(0);
    }
};

template<typename PageOp>
        void map_range(uintptr_t vma_start, uintptr_t vstart, size_t size, PageOp& page_mapper, size_t slop = page_size)
{
    map_level<PageOp, 4> pt_mapper(vma_start, vstart, size, page_mapper, slop);
    pt_mapper(hw_ptep::force(&page_table_root));
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
    bool skip_pte(hw_ptep ptep) {
        return page_mapper.skip_empty() && ptep.read().empty();
    }
    bool descend(hw_ptep ptep) {
        return page_mapper.descend() && !ptep.read().empty() && !ptep.read().large();
    }
    void map_range(uintptr_t vcur, size_t size, PageOp& page_mapper, size_t slop,
            hw_ptep ptep, uintptr_t base_virt)
    {
        map_level<PageOp, level> pt_mapper(vma_start, vcur, size, page_mapper, slop);
        pt_mapper(ptep, base_virt);
    }
    void operator()(hw_ptep parent, uintptr_t base_virt = 0) {
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
                page_mapper.sub_page(parent, ParentLevel, base_virt - vma_start);
                return;
            }
        }
        hw_ptep pt = follow(parent.read());
        phys step = phys(1) << (page_size_shift + level * pte_per_page_shift);
        auto idx = pt_index(vcur, level);
        auto eidx = pt_index(vend, level);
        base_virt += idx * step;
        base_virt = (int64_t(base_virt) << 16) >> 16; // extend 47th bit

        do {
            hw_ptep ptep = pt.at(idx);
            uintptr_t vstart1 = vcur, vend1 = vend;
            clamp(vstart1, vend1, base_virt, base_virt + step - 1, slop);
            if (unsigned(level) < nr_page_sizes && vstart1 == base_virt && vend1 == base_virt + step - 1) {
                uintptr_t offset = base_virt - vma_start;
                if (level) {
                    if (!skip_pte(ptep)) {
                        if (descend(ptep) || !page_mapper.huge_page(ptep, offset)) {
                            page_mapper.intermediate_page_pre(ptep, offset);
                            map_range(vstart1, vend1 - vstart1 + 1, page_mapper, slop, ptep, base_virt);
                            page_mapper.intermediate_page_post(ptep, offset);
                        }
                    }
                } else {
                    if (!skip_pte(ptep)) {
                        page_mapper.small_page(ptep, offset);
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
    unsigned int perm;
    bool _map_dirty;
    pt_element dirty(pt_element pte) {
        pte.set_dirty(_map_dirty);
        return pte;
    }
public:
    populate(page_allocator* pops, unsigned int perm, bool map_dirty = true) :
        _page_provider(pops), perm(perm), _map_dirty(map_dirty) { }
    void small_page(hw_ptep ptep, uintptr_t offset){
        if (!ptep.read().empty()) {
            return;
        }
        phys page = virt_to_phys(_page_provider->alloc(offset));
        if (!ptep.compare_exchange(make_empty_pte(), dirty(make_normal_pte(page, perm)))) {
            _page_provider->free(phys_to_virt(page), offset);
        } else {
            this->account(mmu::page_size);
        }
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset){
        auto pte = ptep.read();
        if (!pte.empty()) {
            return true;
        }
        void *vpage = _page_provider->alloc(huge_page_size, offset);
        if (!vpage) {
            return false;
        }

        phys page = virt_to_phys(vpage);
        if (!ptep.compare_exchange(make_empty_pte(), dirty(make_large_pte(page, perm)))) {
            _page_provider->free(phys_to_virt(page), huge_page_size, offset);
        } else {
            this->account(mmu::huge_page_size);
        }
        return true;
    }
};

struct tlb_gather {
    explicit tlb_gather(page_allocator* provider) : page_provider(provider) {}
    ~tlb_gather() { flush(); }
    static constexpr size_t max_pages = 20;
    struct tlb_page {
        void* addr;
        size_t size;
        off_t offset; // FIXME: unneeded?
    };
    page_allocator* page_provider;
    size_t nr_pages = 0;
    tlb_page pages[max_pages];
    void push(void* addr, size_t size, off_t offset) {
        if (nr_pages == max_pages) {
            flush();
        }
        pages[nr_pages++] = { addr, size, offset };
    }
    void flush() {
        if (!nr_pages) {
            return;
        }
        tlb_flush();
        for (auto i = 0u; i < nr_pages; ++i) {
            auto&& tp = pages[i];
            if (tp.size == page_size) {
                page_provider->free(tp.addr, tp.offset);
            } else {
                page_provider->free(tp.addr, tp.size, tp.offset);
            }
        }
        nr_pages = 0;
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
public:
    unpopulate(page_allocator* pops) : _tlb_gather(pops) {}
    void small_page(hw_ptep ptep, uintptr_t offset) {
        // Note: we free the page even if it is already marked "not present".
        // evacuate() makes sure we are only called for allocated pages, and
        // not-present may only mean mprotect(PROT_NONE).
        pt_element pte = ptep.read();
        ptep.write(make_empty_pte());
        _tlb_gather.push(phys_to_virt(pte.addr(false)), page_size, offset);
        this->account(mmu::page_size);
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset) {
        pt_element pte = ptep.read();
        ptep.write(make_empty_pte());
        _tlb_gather.push(phys_to_virt(pte.addr(true)), huge_page_size, offset);
        this->account(mmu::huge_page_size);
        return true;
    }
    void intermediate_page(hw_ptep ptep, uintptr_t offset) {
        small_page(ptep, offset);
    }
    bool tlb_flush_needed(void) {
        return false; // ~tlb_gather will take care of everything
    }
    void finalize(void) {}
};

class protection : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes> {
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

class count_maps:
    public vma_operation<allocate_intermediate_opt::no,
                         skip_empty_opt::yes, account_opt::yes> {
public:
    void small_page(hw_ptep ptep, uintptr_t offset) {
        this->account(mmu::page_size);
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset) {
        this->account(mmu::huge_page_size);
        return true;
    }
};

template <typename T, account_opt Account = account_opt::no>
class dirty_cleaner : public vma_operation<allocate_intermediate_opt::no, skip_empty_opt::yes, Account> {
private:
    bool do_flush;
    T handler;
public:
    dirty_cleaner(T handler) : do_flush(false), handler(handler) {}
    void small_page(hw_ptep ptep, uintptr_t offset) {
        pt_element pte = ptep.read();
        if (!pte.dirty()) {
            return;
        }
        do_flush |= true;
        pte.set_dirty(false);
        ptep.write(pte);
        handler(ptep.read().addr(false), offset);
    }
    bool huge_page(hw_ptep ptep, uintptr_t offset) {
        pt_element pte = ptep.read();
        if (!pte.dirty()) {
            return true;
        }
        do_flush |= true;
        pte.set_dirty(false);
        ptep.write(pte);
        handler(ptep.read().addr(true), offset, huge_page_size);
        return true;
    }
    bool tlb_flush_needed(void) {return do_flush;}
    void finalize() {
        handler.finalize();
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

class cleanup_intermediate_pages
    : public page_table_operation<
          allocate_intermediate_opt::no,
          skip_empty_opt::yes,
          descend_opt::yes,
          once_opt::no,
          split_opt::no> {
public:
    void small_page(hw_ptep ptep, uintptr_t offset) { ++live_ptes; }
    bool huge_page(hw_ptep ptep, uintptr_t offset) { return true; }
    void intermediate_page_pre(hw_ptep ptep, uintptr_t offset) {
        live_ptes = 0;
    }
    void intermediate_page_post(hw_ptep ptep, uintptr_t offset) {
        if (!live_ptes) {
            auto old = ptep.read();
            auto v = phys_cast<u64*>(old.addr(false));
            for (unsigned i = 0; i < 512; ++i) {
                assert(v[i] == 0);
            }
            ptep.write(make_empty_pte());
            memory::free_page(phys_to_virt(old.addr(false)));
        }
    }
    void sub_page(hw_ptep ptep, int level, uintptr_t offset) {}
private:
    unsigned live_ptes;
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
        tlb_flush();
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
static error protect(const void *addr, size_t size, unsigned int perm)
{
    uintptr_t start = reinterpret_cast<uintptr_t>(addr);
    uintptr_t end = start + size;
    addr_range r(start, end);
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
    abort();
}

ulong evacuate(uintptr_t start, uintptr_t end)
{
    addr_range r(start, end);
    auto range = vma_list.equal_range(r, vma::addr_compare());
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
    addr_range r(start, end);
    auto range = vma_list.equal_range(r, vma::addr_compare());
    for (auto i = range.first; i != range.second; ++i) {
        err = i->sync(start, end);
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
public:
    virtual void* alloc(uintptr_t offset) override {
        return fill(memory::alloc_page(), offset, page_size);
    }
    virtual void* alloc(size_t size, uintptr_t offset) override {
        return fill(memory::alloc_huge_page(size), offset, size);
    }
    virtual void free(void *addr, uintptr_t offset) override {
        return memory::free_page(addr);
    }
    virtual void free(void *addr, size_t size, uintptr_t offset) override {
        return memory::free_huge_page(addr, size);
    }
    virtual void finalize() override {
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

    void finalize() override {
    }
};

class map_file_page_mmap : public page_allocator {
private:
    file* _file;

public:
    map_file_page_mmap(file *file) : _file(file) {}
    virtual ~map_file_page_mmap() {};

    virtual void* alloc(uintptr_t offset) override {
        return _file->get_page(offset, page_size);
    }
    virtual void* alloc(size_t size, uintptr_t offset) override {
        return _file->get_page(offset, size);
    }
    virtual void free(void *addr, uintptr_t offset) override {
        _file->put_page(offset, page_size);
    }
    virtual void free(void *addr, size_t size, uintptr_t offset) override {
        _file->put_page(offset, size);
    }

    void finalize() {
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
    WITH_LOCK(vma_list_mutex) {
        initialized_anonymous_page_provider map;
        operate_range(populate<>(&map, perm_rwx), addr, size);
    }
}

void vdepopulate(void* addr, size_t size)
{
    WITH_LOCK(vma_list_mutex) {
        initialized_anonymous_page_provider map;
        operate_range(unpopulate<>(&map), addr, size);
    }
}

void vcleanup(void* addr, size_t size)
{
    WITH_LOCK(vma_list_mutex) {
        cleanup_intermediate_pages cleaner;
        map_range(reinterpret_cast<uintptr_t>(addr), reinterpret_cast<uintptr_t>(addr), size,
                cleaner, huge_page_size);
    }
}

void* map_anon(const void* addr, size_t size, unsigned flags, unsigned perm)
{
    bool search = !(flags & mmap_fixed);
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto* vma = new mmu::anon_vma(addr_range(start, start + size), perm, flags);
    std::lock_guard<mutex> guard(vma_list_mutex);
    auto v = (void*) allocate(vma, start, size, search);
    if (flags & mmap_populate) {
        vma->operate_range(populate<>(vma->page_ops(), perm, vma->map_dirty()), v, size);
    }
    return v;
}

std::unique_ptr<file_vma> default_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return std::unique_ptr<file_vma>(new file_vma(range, perm, file, offset, flags & mmu::mmap_shared, new map_file_page_read(file, offset)));
}

std::unique_ptr<file_vma> map_file_mmap(file* file, addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return std::unique_ptr<file_vma>(new file_vma(range, perm, file, offset, flags & mmu::mmap_shared, new map_file_page_mmap(file)));
}

void* map_file(const void* addr, size_t size, unsigned flags, unsigned perm,
              fileref f, f_offset offset)
{
    bool search = !(flags & mmu::mmap_fixed);
    auto asize = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    auto *vma = f->mmap(addr_range(start, start + size), flags, perm, offset).release();
    page_allocator *palloc = nullptr;
    void *v;
    WITH_LOCK(vma_list_mutex) {
        v = (void*) allocate(vma, start, asize, search);
        if (flags & mmap_populate) {
            palloc = vma->page_ops();
            vma->operate_range(populate<>(palloc, perm, vma->map_dirty()), v, asize);
        }
    }
    // call finalize outside of the lock so the file read will not happen under it
    if (palloc)
        palloc->finalize();
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
    addr_range r(start, end);

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
        return !(perm & perm_exec);
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
        abort("page fault outside application, addr %lx", addr);
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

vma::vma(addr_range range, unsigned perm, unsigned flags, bool map_dirty, page_allocator *page_ops)
    : _range(align_down(range.start()), align_up(range.end()))
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

unsigned vma::flags() const
{
    return _flags;
}

void vma::update_flags(unsigned flag)
{
    assert(mutex_owned(&vma_list_mutex));
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
    auto hp_start = ::align_up(_range.start(), huge_page_size);
    auto hp_end = ::align_down(_range.end(), huge_page_size);
    size_t size;
    if (hp_start <= addr && addr < hp_end) {
        addr = ::align_down(addr, huge_page_size);
        size = huge_page_size;
    } else {
        size = page_size;
    }

    page_allocator *map = page_ops();
    auto total = operate_range(populate<account_opt::yes>(map, _perm, map_dirty()), (void*)addr, size);
    map->finalize();

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
    _range = addr_range(_range.start(), edge);
    vma_list.insert(*n);
}

error anon_vma::sync(uintptr_t start, uintptr_t end)
{
    return no_error();
}

jvm_balloon_vma::jvm_balloon_vma(uintptr_t start, uintptr_t end, balloon *b, unsigned perm, unsigned flags)
    : vma(addr_range(start, end), perm_rw, 0, true), _balloon(b), _real_perm(perm), _real_flags(flags)
{
}

void jvm_balloon_vma::split(uintptr_t edge)
{
    auto end = _range.end();
    if (edge <= _range.start() || edge >= end) {
        return;
    }
    auto * n = new jvm_balloon_vma(edge, end, _balloon, _real_perm, _real_flags);
    _range = addr_range(_range.start(), edge);
    vma_list.insert(*n);
}

error jvm_balloon_vma::sync(uintptr_t start, uintptr_t end)
{
    return no_error();
}

void jvm_balloon_vma::fault(uintptr_t fault_addr, exception_frame *ef)
{
    std::lock_guard<mutex> guard(vma_list_mutex);
    jvm_balloon_fault(_balloon, ef, this);
    delete this;
}

void jvm_balloon_vma::detach_balloon()
{
    _balloon = nullptr;
    // Could block the creation of the next vma. No need to evacuate, we have no pages
    vma_list.erase(*this);
    mmu::map_anon(addr(), size(), _real_flags, _real_perm);
}

jvm_balloon_vma::~jvm_balloon_vma()
{
    if (_balloon) {
        // This is because the JVM may just decide to unmap a whole region if
        // it believes the objects are no longer valid. It could be the case
        // for a dangling mapping representing a balloon that was already moved
        // out.
        jvm_balloon_fault(_balloon, nullptr, this);
    }
}

// This function marks an anonymous vma as holding the JVM Heap. The JVM may
// create mappings for a variety of reasons, not all of them being the heap.
// Since we're interested in knowing how many pages does the heap hold (to make
// shrinking decisions) we need to mark those regions. The criteria that we'll
// use for that is to, every time we create a jvm_balloon_vma, we mark the
// previous anon vma that was in its place as holding the heap. That will work
// most of the time and with most GC algos. If that is not sufficient, the
// JVM will have to tell us about its regions itself.
static vma *mark_jvm_heap(const void* addr)
{
    WITH_LOCK(vma_list_mutex) {
        u64 a = reinterpret_cast<u64>(addr);
        auto v = vma_list.find(addr_range(a, a+1), vma::addr_compare());
        // It has to be somewhere!
        assert(v != vma_list.end());
        vma& vma = *v;

        if (!vma.has_flags(mmap_jvm_heap)) {
            auto mem = vma.operate_range(count_maps());
            memory::stats::on_jvm_heap_alloc(mem);
        }

        vma.update_flags(mmap_jvm_heap);

        return &vma;
    }
}

ulong map_jvm(const void* addr, size_t size, balloon *b)
{
    auto start = reinterpret_cast<uintptr_t>(addr);

    vma *v = mark_jvm_heap(addr);

    auto* vma = new mmu::jvm_balloon_vma(start, start + size, b, v->perm(), v->flags());
    WITH_LOCK(vma_list_mutex) {
        auto ret = evacuate(start, start + size);
        vma_list.insert(*vma);
        return ret;
    }
    return 0;
}

file_vma::file_vma(addr_range range, unsigned perm, fileref file, f_offset offset, bool shared, page_allocator* page_ops)
    : vma(range, perm, 0, !shared, page_ops)
    , _file(file)
    , _offset(offset)
    , _shared(shared)
{
    int err = validate_perm(perm);

    if (err != 0) {
        throw make_error(err);
    }
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
    _range = addr_range(_range.start(), edge);
    vma_list.insert(*n);
}

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
    void operator()(phys addr, uintptr_t offset) {
        (*this)(addr, offset, page_size);
    }
    void finalize() {
        while(!queue.empty()) {
            elm w = queue.top();
            uio data{&w.iov, 1, w.offset, ssize_t(w.iov.iov_len), UIO_WRITE};
            _file->write(&data, FOF_OFFSET);
            queue.pop();
        }
    }
};

error file_vma::sync(uintptr_t start, uintptr_t end)
{
    if (!_shared)
        return make_error(ENOMEM);
    start = std::max(start, _range.start());
    end = std::min(end, _range.end());
    uintptr_t size = end - start;

    dirty_page_sync sync(_file.get(), _offset, ::size(_file));
    error err = no_error();
    if (operate_range(dirty_cleaner<dirty_page_sync, account_opt::yes>(sync), (void*)start, size) != 0) {
        err = make_error(sys_fsync(_file.get()));
    }
    return err;
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

f_offset file_vma::offset(uintptr_t addr)
{
    return _offset + (addr - _range.start());
}

std::unique_ptr<file_vma> shm_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return map_file_mmap(this, range, flags, perm, offset);
}

void* shm_file::get_page(uintptr_t offset, size_t size)
{
    uintptr_t hp_off = ::align_down(offset, huge_page_size);
    void *addr;

    assert((size != huge_page_size) || (hp_off == offset));

    auto p = _pages.find(hp_off);
    if (p == _pages.end()) {
        addr = memory::alloc_huge_page(huge_page_size);
        memset(addr, 0, huge_page_size);
        _pages.emplace(hp_off, addr);
    } else {
        addr = p->second;
    }
    return static_cast<char*>(addr) + offset - hp_off;
}

void shm_file::put_page(uintptr_t offset, size_t size) {}

shm_file::shm_file(size_t size, int flags) : special_file(flags, DTYPE_UNSPEC), _size(size) {}

int shm_file::stat(struct stat* buf)
{
    buf->st_size = _size;
    return 0;
}

int shm_file::close()
{
    for (auto& i : _pages) {
        memory::free_page(i.second);
    }
    _pages.clear();
    return 0;
}

void linear_map(void* _virt, phys addr, size_t size, size_t slop)
{
    uintptr_t virt = reinterpret_cast<uintptr_t>(_virt);
    slop = std::min(slop, page_size_level(nr_page_sizes - 1));
    assert((virt & (slop - 1)) == (addr & (slop - 1)));
    linear_page_mapper phys_map(addr, size);
    map_range(virt, virt, size, phys_map, slop);
}

void free_initial_memory_range(uintptr_t addr, size_t size)
{
    memory::free_initial_memory_range(phys_cast<void>(addr), size);
}

void switch_to_runtime_page_table()
{
    processor::write_cr3(page_table_root.next_pt_addr());
}

error mprotect(const void *addr, size_t len, unsigned perm)
{
    std::lock_guard<mutex> guard(vma_list_mutex);

    if (!ismapped(addr, len)) {
        return make_error(ENOMEM);
    }

    return protect(addr, len, perm);
}

error munmap(const void *addr, size_t length)
{
    std::lock_guard<mutex> guard(vma_list_mutex);

    if (!ismapped(addr, length)) {
        return make_error(EINVAL);
    }
    sync(addr, length, 0);
    unmap(addr, length);
    return no_error();
}

error msync(const void* addr, size_t length, int flags)
{
    std::lock_guard<mutex> guard(vma_list_mutex);

    if (!ismapped(addr, length)) {
        return make_error(ENOMEM);
    }
    return sync(addr, length, flags);
}

error mincore(const void *addr, size_t length, unsigned char *vec)
{
    char *end = ::align_up((char *)addr + length, page_size);
    char tmp;
    std::lock_guard<mutex> guard(vma_list_mutex);
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
    WITH_LOCK(vma_list_mutex) {
        for (auto& vma : vma_list) {
            char read    = vma.perm() & perm_read  ? 'r' : '-';
            char write   = vma.perm() & perm_write ? 'w' : '-';
            char execute = vma.perm() & perm_exec  ? 'x' : '-';
            char priv    = 'p';
            osv::fprintf(os, "%x-%x %c%c%c%c 00000000 00:00 0\n",
                vma.start(), vma.end(), read, write, execute, priv);
        }
    }
    return os.str();
}

}
