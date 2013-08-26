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

extern void* elf_start;
extern size_t elf_size;

namespace {

typedef boost::format fmt;

constexpr int pte_per_page = 512;
constexpr uintptr_t huge_page_size = mmu::page_size*pte_per_page; // 2 MB

}

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
        insert(*new vma(0, 0));
        uintptr_t e = 0x800000000000;
        insert(*new vma(e, e));
    }
};

__attribute__((init_priority(VMA_LIST_INIT_PRIO)))
vma_list_type vma_list;

// A fairly coarse-grained mutex serializing modifications to both
// vma_list and the page table itself.
mutex vma_list_mutex;

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
        auto v = x & ((u64(1) << 52) - 1);
        v &= ~u64(0xfff);
        v &= ~(u64(large) << 12);
        return v;
    }
    u64 pfn(bool large) const { return addr(large) >> 12; }
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
        auto mask = 0x8000000000000fff | (u64(large) << 12);
        x = (x & mask) | addr;
    }
    void set_pfn(u64 pfn, bool large) { set_addr(pfn << 12, large); }
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

const unsigned nlevels = 4;

void* phys_to_virt(phys pa)
{
    // The ELF is mapped 1:1
    void* phys_addr = reinterpret_cast<void*>(pa);
    if ((phys_addr >= elf_start) && (phys_addr <= elf_start + elf_size)) {
        return phys_addr;
    }

    return phys_mem + pa;
}

phys virt_to_phys_pt(void* virt)
{
    auto v = reinterpret_cast<uintptr_t>(virt);
    auto pte = pt_element::force(processor::read_cr3());
    unsigned level = nlevels;
    while (level > 0 && !pte.large()) {
        assert(pte.present() || level == nlevels);
        --level;
        auto pt = follow(pte);
        pte = pt.at(pt_index(virt, level)).read();
    }
    assert(!pte.empty());
    auto mask = pte_level_mask(level);
    return (pte.addr(level != 0) & mask) | (v & ~mask);
}

phys virt_to_phys(void *virt)
{
    // The ELF is mapped 1:1
    if ((virt >= elf_start) && (virt <= elf_start + elf_size)) {
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

void allocate_intermediate_level(hw_ptep ptep)
{
    phys pt_page = virt_to_phys(memory::alloc_page());
    // since the pt is not yet mapped, we don't need to use hw_ptep
    pt_element* pt = phys_cast<pt_element>(pt_page);
    for (auto i = 0; i < pte_per_page; ++i) {
        pt[i] = make_empty_pte();
    }
    ptep.write(make_normal_pte(pt_page));
}

void free_intermediate_level(hw_ptep ptep)
{
    hw_ptep pt = follow(ptep.read());
    for (auto i = 0; i < pte_per_page; ++i) {
        assert(pt.at(i).read().empty()); // don't free a level which still has pages!
    }
    auto v = pt.release();
    ptep.write(make_empty_pte());
    // FIXME: flush tlb
    memory::free_page(v);
}

void change_perm(hw_ptep ptep, unsigned int perm)
{
    pt_element pte = ptep.read();
    // Note: in x86, if the present bit (0x1) is off, not only read is
    // disallowed, but also write and exec. So in mprotect, if any
    // permission is requested, we must also grant read permission.
    // Linux does this too.
    pte.set_present(perm);
    pte.set_writable(perm & perm_write);
    pte.set_nx(!(perm & perm_exec));
    ptep.write(pte);
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
        phys addend = phys(i) << (12 + 9 * (level - 1));
        tmp.set_addr(tmp.addr(level > 1) | addend, level > 1);
        pt.at(i).write(tmp);
    }
}

struct fill_page {
public:
    virtual void fill(void* addr, uint64_t offset) = 0;
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
    if (sched::cpus.size() == 1)
        return;
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_waiter = sched::thread::current();
    tlb_flush_pendingconfirms.store((int)sched::cpus.size() - 1);
    tlb_flush_ipi.send_allbutself();
    sched::thread::wait_until([] {
            return tlb_flush_pendingconfirms.load() == 0;
    });
}

/*
 * a page_range_operation implementation operates (via the operate() method)
 * on a page-aligned byte range of virtual memory. The range is divided into a
 * bulk of aligned huge pages (2MB pages), and if the beginning and end
 * addresses aren't 2MB aligned, there are additional small pages (4KB pages).
 * The appropriate method (small_page() or huge_page()) is called for each of
 * these pages, to implement the operation.
 * By supporting operations directly on whole huge pages, we allow for smaller
 * pages and better TLB efficiency.
 *
 * TODO: Instead of walking the page table from its root for each page (small
 * or huge), we can more efficiently walk the page table once calling
 * small_page/huge_page for relevant page table entries. See linear_map for
 * an example on how this walk can be done.
 */
class page_range_operation {
public:
    void operate(void *start, size_t size);
    void operate(const vma &vma){ operate((void*)vma.start(), vma.size()); }
protected:
    // offset is the offset of this page in the entire address range
    // (in case the operation needs to know this).
    virtual void small_page(hw_ptep ptep, uintptr_t offset) = 0;
    virtual void huge_page(hw_ptep ptep, uintptr_t offset) = 0;
    virtual bool should_allocate_intermediate() = 0;
private:
    void operate_page(bool huge, void *addr, uintptr_t offset);
};

void page_range_operation::operate(void *start, size_t size)
{
    start = align_down(start, page_size);
    size = align_up(size, page_size);
    void *end = start + size; // one byte after the end

    // Find the largest 2MB-aligned range inside the given byte (or actually,
    // 4K-aligned) range:
    auto hp_start = align_up(start, huge_page_size);
    auto hp_end = align_down(end, huge_page_size);

    // Fix the hp_start/hp_end in degenerate cases so the following
    // loops do the right thing.
    if (hp_start > end) {
        hp_start = end;
    }
    if (hp_end < start) {
        hp_end = end;
    }

    for (void *addr = start; addr < hp_start; addr += page_size) {
        operate_page(false, addr, (uintptr_t)addr-(uintptr_t)start);
    }
    for (void *addr = hp_start; addr < hp_end; addr += huge_page_size) {
        operate_page(true, addr, (uintptr_t)addr-(uintptr_t)start);
    }
    for (void *addr = hp_end; addr < end; addr += page_size) {
        operate_page(false, addr, (uintptr_t)addr-(uintptr_t)start);
    }

    // TODO: consider if instead of requesting a full TLB flush, we should
    // instead try to make more judicious use of INVLPG - e.g., in
    // split_large_page() and other specific places where we modify specific
    // page table entries.
    // TODO: Consider if we're doing tlb_flush() too often, e.g., twice
    // in one mmap which first does evacuate() and then allocate().
    tlb_flush();
}

void page_range_operation::operate_page(bool huge, void *addr, uintptr_t offset)
{
    pt_element pte = pt_element::force(processor::read_cr3());
    auto pt = follow(pte);
    auto ptep = pt.at(pt_index(addr, nlevels - 1));
    unsigned level = nlevels - 1;
    unsigned stopat = huge ? 1 : 0;
    while (level > stopat) {
        pte = ptep.read();
        if (pte.empty()) {
            if (should_allocate_intermediate()) {
                allocate_intermediate_level(ptep);
                pte = ptep.read();
            } else {
                return;
            }
        } else if (pte.large()) {
            // We're trying to change a small page out of a huge page (or
            // in the future, potentially also 2 MB page out of a 1 GB),
            // so we need to first split the large page into smaller pages.
            // Our implementation ensures that it is ok to free pieces of a
            // alloc_huge_page() with free_page(), so it is safe to do such a
            // split.
            split_large_page(ptep, level);
            pte = ptep.read();
        }
        --level;
        pt = follow(pte);
        ptep = pt.at(pt_index(addr, level));
    }
    if(huge) {
        huge_page(ptep, offset);
    } else {
        small_page(ptep, offset);
    }
}

/*
 * populate() populates the page table with the entries it is (assumed to be)
 * missing to span the given virtual-memory address range, and then pre-fills
 * (using the given fill function) these pages and sets their permissions to
 * the given ones. This is part of the mmap implementation.
 */
class populate : public page_range_operation {
private:
    fill_page *fill;
    unsigned int perm;
public:
    populate(fill_page *fill, unsigned int perm) : fill(fill), perm(perm) { }
protected:
    virtual void small_page(hw_ptep ptep, uintptr_t offset){
        phys page = virt_to_phys(memory::alloc_page());
        fill->fill(phys_to_virt(page), offset);
        assert(ptep.read().empty()); // don't populate an already populated page!
        ptep.write(make_normal_pte(page, perm));
    }
    virtual void huge_page(hw_ptep ptep, uintptr_t offset){
        phys page = virt_to_phys(memory::alloc_huge_page(huge_page_size));
        uint64_t o=0;
        // Unfortunately, fill() is only coded for small-page-size chunks, we
        // need to repeat it:
        for (int i=0; i<pte_per_page; i++){
            fill->fill(phys_to_virt(page+o), offset+o);
            o += page_size;
        }
        if (!ptep.read().empty()) {
            assert(!ptep.read().large()); // don't populate an already populated page!
            // held smallpages (already evacuated), now will be used for huge page
            free_intermediate_level(ptep);
        }
        ptep.write(make_large_pte(page, perm));
    }
    virtual bool should_allocate_intermediate(){
        return true;
    }
};

/*
 * Undo the operation of populate(), freeing memory allocated by populate()
 * and marking the pages non-present.
 */
class unpopulate : public page_range_operation {
protected:
    virtual void small_page(hw_ptep ptep, uintptr_t offset){
        // Note: we free the page even if it is already marked "not present".
        // evacuate() makes sure we are only called for allocated pages, and
        // not-present may only mean mprotect(PROT_NONE).
        pt_element pte = ptep.read();
        assert(!pte.empty()); // evacuate() shouldn't call us twice for the same page.
        ptep.write(make_empty_pte());
        // FIXME: tlb flush
        memory::free_page(phys_to_virt(pte.addr(false)));
    }
    virtual void huge_page(hw_ptep ptep, uintptr_t offset){
        pt_element pte = ptep.read();
        ptep.write(make_empty_pte());
        // FIXME: tlb flush
        assert(!pte.empty()); // evacuate() shouldn't call us twice for the same page.
        if (pte.large()) {
            memory::free_huge_page(phys_to_virt(pte.addr(true)),
                    huge_page_size);
        } else {
            // We've previously allocated small pages here, not a huge pages.
            // We need to free them one by one - as they are not necessarily part
            // of one huge page.
            hw_ptep pt = follow(pte);
            for(int i=0; i<pte_per_page; ++i) {
                assert(!pt.at(i).read().empty()); //  evacuate() shouldn't call us twice for the same page.
                pt_element pte = pt.at(i).read();
                // FIXME: tlb flush?
                pt.at(i).write(make_empty_pte());
                memory::free_page(phys_to_virt(pte.addr(false)));
            }
            memory::free_page(pt.release());
        }
    }
    virtual bool should_allocate_intermediate(){
        return false;
    }
};

class protection : public page_range_operation {
private:
    unsigned int perm;
    bool success;
public:
    protection(unsigned int perm) : perm(perm), success(true) { }
    bool getsuccess(){ return success; }
protected:
    virtual void small_page(hw_ptep ptep, uintptr_t offset){
         if (ptep.read().empty()) {
            success = false;
            return;
        }
        change_perm(ptep, perm);
     }
    virtual void huge_page(hw_ptep ptep, uintptr_t offset){
        if (ptep.read().empty()) {
            success = false;
        } else if (ptep.read().large()) {
            change_perm(ptep, perm);
        } else {
            hw_ptep pt = follow(ptep.read());
            for (int i=0; i<pte_per_page; ++i) {
                if (!pt.at(i).read().empty()) {
                    change_perm(pt.at(i), perm);
                } else {
                    success = false;
                }
            }
        }
    }
    virtual bool should_allocate_intermediate(){
        success = false;
        return false;
    }
};

int protect(void *addr, size_t size, unsigned int perm)
{
    std::lock_guard<mutex> guard(vma_list_mutex);
    protection p(perm);
    p.operate(addr, size);
    return p.getsuccess();
}

uintptr_t find_hole(uintptr_t start, uintptr_t size)
{
    // FIXME: use lower_bound or something
    auto p = vma_list.begin();
    auto n = std::next(p);
    while (n != vma_list.end()) {
        if (start >= p->end() && start + size <= n->start()) {
            return start;
        }
        if (p->end() >= start && n->start() - p->end() >= size) {
            return p->end();
        }
        p = n;
        ++n;
    }
    abort();
}

bool contains(uintptr_t start, uintptr_t end, vma& y)
{
    return y.start() >= start && y.end() <= end;
}

void evacuate(uintptr_t start, uintptr_t end)
{
    std::lock_guard<mutex> guard(vma_list_mutex);
    // FIXME: use equal_range or something
    for (auto i = std::next(vma_list.begin());
            i != std::prev(vma_list.end());
            ++i) {
        i->split(end);
        i->split(start);
        if (contains(start, end, *i)) {
            auto& dead = *i--;
            unpopulate().operate(dead);
            vma_list.erase(dead);
            delete &dead;
        }
    }
}

void unmap(void* addr, size_t size)
{
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    evacuate(start, start+size);
}

struct fill_anon_page : fill_page {
    virtual void fill(void* addr, uint64_t offset) {
        memset(addr, 0, page_size);
    }
};

uintptr_t allocate(uintptr_t start, size_t size, bool search,
                    fill_page& fill, unsigned perm)
{
    // To support memory allocation tracking - where operator new() might end
    // up indirectly calling mmap(), we need to allocate the vma object here,
    // and not between the evacuate() and populate().
    vma *v = new vma(start, start + size);

    std::lock_guard<mutex> guard(vma_list_mutex);
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

    populate(&fill, perm).operate((void*)start, size);

    return start;
}

void vpopulate(void* addr, size_t size)
{
    fill_anon_page fill;
    WITH_LOCK(vma_list_mutex) {
        populate(&fill, perm_rwx).operate(addr, size);
    }
}

void vdepopulate(void* addr, size_t size)
{
    WITH_LOCK(vma_list_mutex) {
        unpopulate().operate(addr, size);
    }
    tlb_flush();
}

void* map_anon(void* addr, size_t size, bool search, unsigned perm)
{
    size = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    fill_anon_page zfill;
    return (void*) allocate(start, size, search, zfill, perm);
}

void* map_file(void* addr, size_t size, bool search, unsigned perm,
              fileref f, f_offset offset)
{
    auto asize = align_up(size, mmu::page_size);
    auto start = reinterpret_cast<uintptr_t>(addr);
    fill_anon_page zfill;
    auto v = (void*) allocate(start, asize, search, zfill, perm | perm_write);
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

// Efficiently find the vma in vma_list which contains the given address.
// Performance is logarithmic in length of vma_list, so it is more efficient
// than simple iteration. Returns vma_list.end() if the address isn't mapped.
vma_list_type::iterator find_vma(uintptr_t addr)
{
    auto p = vma_list.lower_bound(vma(addr,addr));
    if (p == vma_list.end() || p->start() == addr) {
        return p;
    } else {
        // p is the first with p->start() > addr. So p doesn't contain addr,
        // but the previous vma may, and we need to check.
        if (p == vma_list.begin()) {
            return vma_list.end();
        } else {
            --p;
            if (p->start() <= addr && addr < p->end()) {
                return p;
            } else
                return vma_list.end();
        }
    }
}

// Checks if the entire given memory region is mapped (in vma_list).
bool ismapped(void *addr, size_t size)
{
    uintptr_t start = (uintptr_t) addr;
    uintptr_t end = start + size;

    std::lock_guard<mutex> guard(vma_list_mutex);

    for (auto p = find_vma(start); p != vma_list.end(); ++p) {
        if (p->start() > start)
            return false;
        start = p->end();
        if (start >= end)
            return true;
    }
    return false;
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

vma::vma(uintptr_t start, uintptr_t end)
    : _start(align_down(start))
    , _end(align_up(end))
{
}

void vma::set(uintptr_t start, uintptr_t end)
{
    _start = align_down(start);
    _end = align_up(end);
}

uintptr_t vma::start() const
{
    return _start;
}

uintptr_t vma::end() const
{
    return _end;
}

void* vma::addr() const
{
    return reinterpret_cast<void*>(_start);
}

uintptr_t vma::size() const
{
    return _end - _start;
}

void vma::split(uintptr_t edge)
{
    if (edge <= _start || edge >= _end) {
        return;
    }
    vma* n = new vma(edge, _end);
    _end = edge;
    vma_list.insert(*n);
}

unsigned nr_page_sizes = 2; // FIXME: detect 1GB pages

void set_nr_page_sizes(unsigned nr)
{
    nr_page_sizes = nr;
}

pt_element page_table_root;

void clamp(uintptr_t& vstart1, uintptr_t& vend1,
           uintptr_t min, size_t max, size_t slop)
{
    vstart1 &= ~(slop - 1);
    vend1 |= (slop - 1);
    vstart1 = std::max(vstart1, min);
    vend1 = std::min(vend1, max);
}

unsigned pt_index(uintptr_t virt, unsigned level)
{
    return pt_index(reinterpret_cast<void*>(virt), level);
}

void linear_map_level(hw_ptep parent, uintptr_t vstart, uintptr_t vend,
        phys delta, uintptr_t base_virt, size_t slop, unsigned level)
{
    --level;
    if (!parent.read().present()) {
        allocate_intermediate_level(parent);
    }
    hw_ptep pt = follow(parent.read());
    phys step = phys(1) << (12 + level * 9);
    auto idx = pt_index(vstart, level);
    auto eidx = pt_index(vend, level);
    base_virt += idx * step;
    base_virt = (s64(base_virt) << 16) >> 16; // extend 47th bit
    while (idx <= eidx) {
        uintptr_t vstart1 = vstart, vend1 = vend;
        clamp(vstart1, vend1, base_virt, base_virt + step - 1, slop);
        if (level < nr_page_sizes && vstart1 == base_virt && vend1 == base_virt + step - 1) {
            pt.at(idx).write(make_pte(vstart1 + delta, level > 0));
        } else {
            linear_map_level(pt.at(idx), vstart1, vend1, delta, base_virt, slop, level);
        }
        base_virt += step;
        ++idx;
    }
}

size_t page_size_level(unsigned level)
{
    return size_t(1) << (12 + 9 * level);
}

void linear_map(void* _virt, phys addr, size_t size, size_t slop)
{
    uintptr_t virt = reinterpret_cast<uintptr_t>(_virt);
    slop = std::min(slop, page_size_level(nr_page_sizes - 1));
    assert((virt & (slop - 1)) == (addr & (slop - 1)));
    linear_map_level(hw_ptep::force(&page_table_root), virt, virt + size - 1,
            addr - virt, 0, slop, 4);
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

void page_fault(exception_frame *ef)
{
    extern const char text_start[], text_end[];
    sched::exception_guard g;
    auto addr = processor::read_cr2();
    if (fixup_fault(ef)) {
        return;
    }
    auto pc = reinterpret_cast<void*>(ef->rip);
    if (!pc) {
        abort("trying to execute null pointer");
    }
    if (pc >= text_start && pc < text_end) {
        abort("page fault outside application");
    }
    osv::handle_segmentation_fault(addr, ef);
}
