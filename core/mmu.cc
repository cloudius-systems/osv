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

namespace {

typedef boost::format fmt;

constexpr uintptr_t page_size = 4096;
constexpr int pte_per_page = 512;
constexpr uintptr_t huge_page_size = page_size*pte_per_page; // 2 MB

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

vma_list_type vma_list;

// A fairly coarse-grained mutex serializing modifications to both
// vma_list and the page table itself.
mutex vma_list_mutex;

typedef uint64_t pt_element;
const unsigned nlevels = 4;

void* phys_to_virt(phys pa)
{
    return phys_mem + pa;
}

phys virt_to_phys(void *virt)
{
    // For now, only allow non-mmaped areas.  Later, we can either
    // bounce such addresses, or lock them in memory and translate
    assert(virt >= phys_mem);
    return static_cast<char*>(virt) - phys_mem;
}

unsigned pt_index(void *virt, unsigned level)
{
    auto v = reinterpret_cast<ulong>(virt);
    return (v >> (12 + level * 9)) & 511;
}

phys pte_phys(pt_element pte)
{
    pt_element mask = (((pt_element(1) << 53) - 1)
            & ~((pt_element(1) << 12) - 1));
    return pte & mask;
}

bool pte_present(pt_element pte)
{
    return pte & 1;
}

void allocate_intermediate_level(pt_element *ptep)
{
    phys pt_page = virt_to_phys(memory::alloc_page());
    pt_element* pt = phys_cast<pt_element>(pt_page);
    for (auto i = 0; i < pte_per_page; ++i) {
        pt[i] = 0;
    }
    *ptep = pt_page | 0x63;
}

void free_intermediate_level(pt_element *ptep)
{
    phys page=pte_phys(*ptep);
    assert(page);
    pt_element *pt = phys_cast<pt_element>(page);
    for (auto i = 0; i < pte_per_page; ++i) {
        assert(pt[i]==0); // don't free a level which still has pages!
    }
    memory::free_page(phys_to_virt(page));
    *ptep=0;
}

void change_perm(pt_element *ptep, unsigned int perm)
{
    // Note: in x86, if the present bit (0x1) is off, not only read is
    // disallowed, but also write and exec. So in mprotect, if any
    // permission is requested, we must also grant read permission.
    // Linux does this too.
    if (perm)
        *ptep |= 0x1;
    else
        *ptep &= ~0x1;

    if (perm & perm_write)
        *ptep |= 0x2;
    else
        *ptep &= ~0x2;

    if (!(perm & perm_exec))
        *ptep |= pt_element(0x8000000000000000);
    else
        *ptep &= ~pt_element(0x8000000000000000);
}

pt_element make_pte(phys addr, unsigned perm)
{
    pt_element pte = addr | 0x60;
    change_perm(&pte, perm);
    return pte;
}

bool pte_large(pt_element pt)
{
    return pt & (1 << 7);
}

void split_large_page(pt_element* ptep, unsigned level)
{
    auto pte_orig = *ptep;
    if (level == 1) {
        pte_orig &= ~pt_element(1 << 7);
    }
    allocate_intermediate_level(ptep);
    auto pt = phys_cast<pt_element>(pte_phys(*ptep));
    for (auto i = 0; i < pte_per_page; ++i) {
        pt[i] = pte_orig | (pt_element(i) << (12 + 9 * (level - 1)));
    }
}

struct fill_page {
public:
    virtual void fill(void* addr, uint64_t offset) = 0;
};

void debug_count_ptes(pt_element pte, int level, size_t &nsmall, size_t &nhuge)
{
    if (level<4 && !pte_present(pte)){
        // nothing
    } else if (pte_large(pte)){
        nhuge++;
    } else if (level==0){
        nsmall++;
    } else {
        pt_element* pt = phys_cast<pt_element>(pte_phys(pte));
        for(int i=0; i<pte_per_page; ++i) {
            debug_count_ptes(pt[i], level-1, nsmall, nhuge);
        }
    }
}


void tlb_flush_this_processor()
{
   processor::write_cr3(processor::read_cr3());
}

// tlb_flush() does TLB flush on *all* processors, not returning before all
// processors confirm flushing their TLB. This is slow :(
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
    std::lock_guard<mutex> guard(tlb_flush_mutex);
    tlb_flush_this_processor();
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
    void operate(vma &vma){ operate((void*)vma.start(), vma.size()); }
protected:
    // offset is the offset of this page in the entire address range
    // (in case the operation needs to know this).
    virtual void small_page(pt_element *ptep, uintptr_t offset) = 0;
    virtual void huge_page(pt_element *ptep, uintptr_t offset) = 0;
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
    pt_element pte = processor::read_cr3();
    auto pt = phys_cast<pt_element>(pte_phys(pte));
    auto ptep = &pt[pt_index(addr, nlevels - 1)];
    unsigned level = nlevels - 1;
    unsigned stopat = huge ? 1 : 0;
    while (level > stopat) {
        if (!pte_present(*ptep)) {
            if (should_allocate_intermediate()) {
                allocate_intermediate_level(ptep);
            } else {
                return;
            }
        } else if (pte_large(*ptep)) {
            // We're trying to change a small page out of a huge page (or
            // in the future, potentially also 2 MB page out of a 1 GB),
            // so we need to first split the large page into smaller pages.
            // Our implementation ensures that it is ok to free pieces of a
            // alloc_huge_page() with free_page(), so it is safe to do such a
            // split.
            split_large_page(ptep, level);
        }
        pte = *ptep;
        --level;
        pt = phys_cast<pt_element>(pte_phys(pte));
        ptep = &pt[pt_index(addr, level)];
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
    virtual void small_page(pt_element *ptep, uintptr_t offset){
        phys page = virt_to_phys(memory::alloc_page());
        fill->fill(phys_to_virt(page), offset);
        assert(*ptep==0); // don't populate an already populated page!
        *ptep = make_pte(page, perm);
    }
    virtual void huge_page(pt_element *ptep, uintptr_t offset){
        phys page = virt_to_phys(memory::alloc_huge_page(huge_page_size));
        uint64_t o=0;
        // Unfortunately, fill() is only coded for small-page-size chunks, we
        // need to repeat it:
        for (int i=0; i<pte_per_page; i++){
            fill->fill(phys_to_virt(page+o), offset+o);
            o += page_size;
        }
        if (*ptep) {
            assert(!pte_large(*ptep)); // don't populate an already populated page!
            // held smallpages (already evacuated), now will be used for huge page
            free_intermediate_level(ptep);
        }
        *ptep = make_pte(page, perm) | (1<<7);
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
    virtual void small_page(pt_element *ptep, uintptr_t offset){
        // Note: we free the page even if it is already marked "not present".
        // evacuate() makes sure we are only called for allocated pages, and
        // not-present may only mean mprotect(PROT_NONE).
        assert(*ptep); // evacuate() shouldn't call us twice for the same page.
        memory::free_page(phys_to_virt(pte_phys(*ptep)));
        *ptep = 0;
    }
    virtual void huge_page(pt_element *ptep, uintptr_t offset){
        if (!pte_present(*ptep)) {
            // Note: we free the page even if it is already marked "not present".
            // evacuate() makes sure we are only called for allocated pages, and
            // not-present may only mean mprotect(PROT_NONE).
            assert(*ptep); // evacuate() shouldn't call us twice for the same page.
            memory::free_huge_page(phys_to_virt(pte_phys(*ptep)),
                    huge_page_size);
        } else if (pte_large(*ptep)) {
            memory::free_huge_page(phys_to_virt(pte_phys(*ptep)),
                    huge_page_size);
        } else {
            // We've previously allocated small pages here, not a huge pages.
            // We need to free them one by one - as they are not necessarily part
            // of one huge page.
            pt_element* pt = phys_cast<pt_element>(pte_phys(*ptep));
            for(int i=0; i<pte_per_page; ++i) {
                assert(pt[i]); //  evacuate() shouldn't call us twice for the same page.
                memory::free_page(phys_to_virt(pte_phys(pt[i])));
            }
        }
        *ptep = 0;
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
    virtual void small_page(pt_element *ptep, uintptr_t offset){
         if (!*ptep) {
            success = false;
            return;
        }
        change_perm(ptep, perm);
     }
    virtual void huge_page(pt_element *ptep, uintptr_t offset){
        if (!*ptep) {
            success = false;
        } else if (pte_large(*ptep)) {
            change_perm(ptep, perm);
        } else {
            pt_element* pt = phys_cast<pt_element>(pte_phys(*ptep));
            for (int i=0; i<pte_per_page; ++i) {
                if (pt[i]) {
                    change_perm(&pt[i], perm);
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
    auto start = reinterpret_cast<uintptr_t>(addr);
    evacuate(start, start+size);
}

struct fill_anon_page : fill_page {
    virtual void fill(void* addr, uint64_t offset) {
        memset(addr, 0, page_size);
    }
};

struct fill_file_page : fill_page {
    fill_file_page(file& _f, uint64_t _off, uint64_t _len)
        : f(_f), off(_off), len(_len) {}
    virtual void fill(void* addr, uint64_t offset) {
        offset += off;
        unsigned toread = 0;
        if (offset < len) {
            toread = std::min(len - offset, page_size);
           f.read(addr, offset, toread);
        }
        memset(addr + toread, 0, page_size - toread);
    }
    file& f;
    uint64_t off;
    uint64_t len;
};

uintptr_t allocate(uintptr_t start, size_t size, bool search,
                    fill_page& fill, unsigned perm)
{
    std::lock_guard<mutex> guard(vma_list_mutex);

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

    vma* v = new vma(start, start+size);
    vma_list.insert(*v);

    populate(&fill, perm).operate((void*)start, size);

    return start;
}

void* map_anon(void* addr, size_t size, bool search, unsigned perm)
{
    auto start = reinterpret_cast<uintptr_t>(addr);
    fill_anon_page zfill;
    return (void*) allocate(start, size, search, zfill, perm);
}

void* map_file(void* addr, size_t size, bool search, unsigned perm,
              file& f, f_offset offset)
{
    auto start = reinterpret_cast<uintptr_t>(addr);
    fill_file_page ffill(f, offset, f.size());
    return (void*) allocate(start, size, search, ffill, perm);
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

void linear_map_level(pt_element& parent, uintptr_t vstart, uintptr_t vend,
        phys delta, uintptr_t base_virt, size_t slop, unsigned level)
{
    --level;
    if (!(parent & 1)) {
        allocate_intermediate_level(&parent);
    }
    pt_element* pt = phys_cast<pt_element>(pte_phys(parent));
    pt_element step = pt_element(1) << (12 + level * 9);
    auto idx = pt_index(vstart, level);
    auto eidx = pt_index(vend, level);
    base_virt += idx * step;
    base_virt = (s64(base_virt) << 16) >> 16; // extend 47th bit
    while (idx <= eidx) {
        uintptr_t vstart1 = vstart, vend1 = vend;
        clamp(vstart1, vend1, base_virt, base_virt + step - 1, slop);
        if (level < nr_page_sizes && vstart1 == base_virt && vend1 == base_virt + step - 1) {
            pt[idx] = (vstart1 + delta) | 0x67 | (level == 0 ? 0 : 0x80);
        } else {
            linear_map_level(pt[idx], vstart1, vend1, delta, base_virt, slop, level);
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
    linear_map_level(page_table_root, virt, virt + size - 1,
            addr - virt, 0, slop, 4);
}

void free_initial_memory_range(uintptr_t addr, size_t size)
{
    memory::free_initial_memory_range(phys_cast<void>(addr), size);
}

void switch_to_runtime_page_table()
{
    processor::write_cr3(pte_phys(page_table_root));
}

}

void page_fault(exception_frame *ef)
{
    auto addr = processor::read_cr2();
    // FIXME: handle fixable faults
    osv::handle_segmentation_fault(addr, ef);
}
