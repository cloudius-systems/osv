#include "mmu.hh"
#include "mempool.hh"
#include "processor.hh"
#include "debug.hh"
#include "exceptions.hh"
#include <boost/format.hpp>
#include <string.h>
#include <iterator>
#include "libc/signal.hh"

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

typedef uint64_t pt_element;
const unsigned nlevels = 4;

void* phys_to_virt(phys pa)
{
    return reinterpret_cast<void*>(pa + 0xffff800000000000ull);
}

phys virt_to_phys(void *virt)
{
    // For now, only allow non-mmaped areas.  Later, we can either
    // bounce such addresses, or lock them in memory and translate
    assert(reinterpret_cast<phys>(virt) >= 0xffff800000000000ull);
    return reinterpret_cast<phys>(virt) - 0xffff800000000000ull;
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

phys alloc_page()
{
    void *p = memory::alloc_page();
    return virt_to_phys(p);
}

void allocate_intermediate_level(pt_element *ptep)
{
    phys pt_page = alloc_page();
    pt_element* pt = phys_cast<pt_element>(pt_page);
    for (auto i = 0; i < pte_per_page; ++i) {
        pt[i] = 0;
    }
    *ptep = pt_page | 0x63;
}

pt_element make_pte(phys addr, unsigned perm)
{
    pt_element pte = addr | 0x61;
    if (perm & perm_write) {
        pte |= 0x2;
    }
    if (!(perm * perm_exec)) {
        pte |= pt_element(0x8000000000000000);
    }
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
    // FIXME: tlb flush
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

void populate_page(void* addr, fill_page& fill, uint64_t offset, unsigned perm)
{
    pt_element pte = processor::read_cr3();
    auto pt = phys_cast<pt_element>(pte_phys(pte));
    auto ptep = &pt[pt_index(addr, nlevels - 1)];
    unsigned level = nlevels - 1;
    while (level > 0) {
        if (!pte_present(*ptep)) {
            allocate_intermediate_level(ptep);
        } else if (pte_large(*ptep)) {
            split_large_page(ptep, level);
        }
        pte = *ptep;
        --level;
        pt = phys_cast<pt_element>(pte_phys(pte));
        ptep = &pt[pt_index(addr, level)];
    }
    phys page = alloc_page();
    fill.fill(phys_to_virt(page), offset);
    *ptep = make_pte(page, perm);
}

void populate_huge_page(void* addr, fill_page& fill, uint64_t offset, unsigned perm)
{
    pt_element pte = processor::read_cr3();
    auto pt = phys_cast<pt_element>(pte_phys(pte));
    auto ptep = &pt[pt_index(addr, nlevels - 1)];
    unsigned level = nlevels - 1;
    while (level > 1) {
        if (!pte_present(*ptep)) {
            allocate_intermediate_level(ptep);
        } else if (pte_large(*ptep)) {
            split_large_page(ptep, level);
        }
        pte = *ptep;
        --level;
        pt = phys_cast<pt_element>(pte_phys(pte));
        ptep = &pt[pt_index(addr, level)];
    }
    phys page = virt_to_phys(memory::alloc_huge_page(huge_page_size));
    uint64_t o=0;
    for (int i=0; i<pte_per_page; i++){
        fill.fill(phys_to_virt(page+o), offset+o);
        o += page_size;
    }
    *ptep = make_pte(page, perm) | (1<<7);
}

/*
 * populate() populates the page table with the entries it is (assumed to be)
 * missing to span the given virtual-memory address range vma, and then
 * pre-fills (using the given fill function) these pages and sets their
 * permissions to the given ones. This is part of the mmap implementation.
 *
 * If full huge (2MB) pages fit inside this range, they are used for smaller
 * page tables and better TLB efficiency. However, if the start or end address
 * is not 2MB aligned, we will need to apply the fill and perm only to a part
 * of a large page, in which case we must break the entire large page into its
 * constitutive small (4K) pages.
 *
 * FIXME: It would be nicer to, instead of iterating on all levels per page as
 * we do in populate_page/populate_huge_page, we walk once on the whole
 * hiearchy, as in linear_map.
 */
void populate(vma& vma, fill_page& fill, unsigned perm)
{
    // Find the largest 2MB-aligned range inside the given byte (or actually,
    // 4K-aligned) range:
    uintptr_t hp_start = ((vma.start()-1) & ~(huge_page_size-1)) + huge_page_size;
    uintptr_t hp_end = (vma.end()) & ~(huge_page_size-1);

    if (hp_start > vma.end())
        hp_start = vma.end();
    if (hp_end < vma.start())
        hp_end = vma.start();

    /* Step 1: Break up the partial huge page (if any) in the beginning of the
     * address range, and populate the small pages.
     *  TODO: it would be more efficient not to walk all the levels all the time */
    for (auto addr = vma.start(); addr < hp_start; addr += page_size)
        populate_page(reinterpret_cast<void*>(addr), fill, addr-vma.start(), perm);
    /* Step 2: Populate the huge pages (if any) in the middle of the range */
    for (auto addr = hp_start; addr < hp_end; addr += huge_page_size)
        populate_huge_page(reinterpret_cast<void*>(addr), fill, addr-vma.start(), perm);
    /* Step 3: Break up the partial huge page (if any) at the end of the range */
    for (auto addr = hp_end; addr < vma.end(); addr += page_size)
        populate_page(reinterpret_cast<void*>(addr), fill, addr-vma.start(), perm);
    //size_t nsmall=0, nhuge=0;
    //debug_count_ptes(processor::read_cr3(), 4, nsmall, nhuge);
    //debug(fmt("after population, page table contains %ld small pages, %ld huge") % nsmall % nhuge);

}

void unpopulate_page(void* addr)
{
    pt_element pte = processor::read_cr3();
    auto pt = phys_cast<pt_element>(pte_phys(pte));
    auto ptep = &pt[pt_index(addr, nlevels - 1)];
    unsigned level = nlevels - 1;
    while (level > 0) {
        if (!pte_present(*ptep))
            return;
        else if (pte_large(*ptep)) {
            // This case means that part of a larger mmap was mmapped over,
            // previously a huge page was mapped, and now we need to free some
            // of the small pages composing it. Luckily, in our implementation
            // it is ok to free pieces of a alloc_huge_page() with free_page()
            split_large_page(ptep, level);
        }
        assert(!pte_large(*ptep));
        pte = *ptep;
        --level;
        pt = phys_cast<pt_element>(pte_phys(pte));
        ptep = &pt[pt_index(addr, level)];
    }
    if (!pte_present(*ptep))
        return;
    *ptep &= ~1; // make not present
    memory::free_page(phys_to_virt(pte_phys(*ptep)));
}

void unpopulate_huge_page(void* addr)
{
    pt_element pte = processor::read_cr3();
    auto pt = phys_cast<pt_element>(pte_phys(pte));
    auto ptep = &pt[pt_index(addr, nlevels - 1)];
    unsigned level = nlevels - 1;
    while (level > 1) {
        if (!pte_present(*ptep))
            return;
        else if (pte_large(*ptep))
            split_large_page(ptep, level);
        pte = *ptep;
        --level;
        pt = phys_cast<pt_element>(pte_phys(pte));
        ptep = &pt[pt_index(addr, level)];
    }
    if (!pte_present(*ptep))
        return;
    if (pte_large(*ptep)){
        memory::free_huge_page(phys_to_virt(pte_phys(*ptep)), huge_page_size);
    } else {
        // We've previously allocated small pages here, not a huge pages.
        // We need to free them one by one - as they are not necessarily part
        // of one huge page.
        pt_element* pt = phys_cast<pt_element>(pte_phys(*ptep));
        for(int i=0; i<pte_per_page; ++i)
            if (pte_present(pt[i]))
                memory::free_page(phys_to_virt(pte_phys(pt[i])));
    }
    *ptep &= ~1; // make not present
}

/*
 * Undo the operation of populate(), freeing memory allocated by populate()
 * and marking the pages non-present.
 */
void unpopulate(vma& vma)
{
    uintptr_t hp_start = ((vma.start()-1) & ~(huge_page_size-1)) + huge_page_size;
    uintptr_t hp_end = (vma.end()) & ~(huge_page_size-1);
    if (hp_start > vma.end())
        hp_start = vma.end();
    if (hp_end < vma.start())
        hp_end = vma.start();

    for (auto addr = vma.start(); addr < hp_start; addr += page_size)
        unpopulate_page(reinterpret_cast<void*>(addr));
    for (auto addr = hp_start; addr < hp_end; addr += huge_page_size)
        unpopulate_huge_page(reinterpret_cast<void*>(addr));
    for (auto addr = hp_end; addr < vma.end(); addr += page_size)
        unpopulate_page(reinterpret_cast<void*>(addr));
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

bool contains(vma& x, vma& y)
{
    return y.start() >= x.start() && y.end() <= x.end();
}

void evacuate(vma* v)
{
    // FIXME: use equal_range or something
    for (auto i = std::next(vma_list.begin());
            i != std::prev(vma_list.end());
            ++i) {
        i->split(v->end());
        i->split(v->start());
        if (contains(*v, *i)) {
            auto& dead = *i--;
            unpopulate(dead);
            vma_list.erase(dead);
        }
    }
}

vma* reserve(void* hint, size_t size)
{
    // look for a hole around 'hint'
    auto start = reinterpret_cast<uintptr_t>(hint);
    if (!start) {
        start = 0x200000000000ul;
    }
    start = find_hole(start, size);
    auto v = new vma(start, start + size);
    vma_list.insert(*v);
    return v;
}

void unmap(void* addr, size_t size)
{
    auto start = reinterpret_cast<uintptr_t>(addr);
    vma tmp { start, start+size };
    evacuate(&tmp);
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

vma* allocate(uintptr_t start, uintptr_t end, fill_page& fill,
        unsigned perm)
{
    vma* ret = new vma(start, end);
    evacuate(ret);
    vma_list.insert(*ret);

    populate(*ret, fill, perm);

    return ret;
}

vma* map_anon(void* addr, size_t size, unsigned perm)
{
    auto start = reinterpret_cast<uintptr_t>(addr);
    fill_anon_page zfill;
    return allocate(start, start + size, zfill, perm);
}

vma* map_file(void* addr, size_t size, unsigned perm,
              file& f, f_offset offset)
{
    auto start = reinterpret_cast<uintptr_t>(addr);
    fill_file_page ffill(f, offset, f.size());
    vma* ret = allocate(start, start + size, ffill, perm);
    return ret;
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

void linear_map(uintptr_t virt, phys addr, size_t size, size_t slop)
{
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
