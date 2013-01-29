#include "mmu.hh"
#include "mempool.hh"
#include "processor.hh"
#include "debug.hh"
#include "exceptions.hh"
#include <boost/format.hpp>
#include <string.h>
#include <iterator>

namespace {
    typedef boost::format fmt;
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

    template <typename T>
    T* phys_cast(phys pa)
    {
	return static_cast<T*>(phys_to_virt(pa));
    }

    phys virt_to_phys(void *virt)
    {
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
	for (auto i = 0; i < 512; ++i) {
	    pt[i] = 0;
	}
	*ptep = pt_page | 0x63;
    }

    pt_element make_pte(phys addr)
    {
        return addr | 0x63;
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
        for (auto i = 0; i < 512; ++i) {
            pt[i] = pte_orig | (pt_element(i) << (12 + 9 * (level - 1)));
        }
        // FIXME: tlb flush
    }

    void populate_page(void* addr)
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
	*ptep = make_pte(alloc_page());
    }

    void populate(vma& vma)
    {
	// FIXME: don't iterate all levels per page
	// FIXME: use large pages
	for (auto addr = vma.addr();
	     addr < vma.addr() + vma.size();
	     addr += 4096) {
	    populate_page(addr);
	}
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
        vma tmp { reinterpret_cast<uintptr_t>(addr), size };
        evacuate(&tmp);
    }

    vma* map_anon_dontzero(uintptr_t start, uintptr_t end, unsigned perm)
    {
        vma* ret = new vma(start, end);
        evacuate(ret);
        vma_list.insert(*ret);

        populate(*ret);

        return ret;
    }

    vma* map_anon(void* addr, size_t size, unsigned perm)
    {
        auto start = reinterpret_cast<uintptr_t>(addr);
        auto ret = map_anon_dontzero(start, start + size, perm);
        memset(addr, 0, size);
        return ret;
    }

    vma* map_file(void* addr, size_t size, unsigned perm,
                  file& f, f_offset offset)
    {
        auto start = reinterpret_cast<uintptr_t>(addr);
        auto fsize = f.size();
        if (offset >= fsize) {
            return map_anon(addr, size, perm);
        }
        vma* ret = map_anon_dontzero(start, start + size, perm);
        auto rsize = std::min(offset + size, fsize) - offset;
        f.read(addr, offset, rsize);
        memset(addr + rsize, 0, size - rsize);
        return ret;
    }

    namespace {
        const uintptr_t page_size = 4096;

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
    debug(fmt("page fault @ %1$x") % addr);
    abort();
}
