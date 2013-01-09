#include "mmu.hh"
#include "mempool.hh"
#include "processor.hh"
#include "debug.hh"
#include "exceptions.hh"
#include <boost/format.hpp>
#include <string.h>

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

    boost::intrusive::set<vma,
			  bi::compare<vma_compare>,
			  bi::member_hook<vma,
					  bi::set_member_hook<>,
					  &vma::_vma_list_hook>,
			  bi::optimize_size<true>
			  > vma_list;

    typedef uint64_t pt_element;
    const unsigned nlevels = 4;

    template <typename T>
    T* phys_cast(phys pa)
    {
	return reinterpret_cast<T*>(pa);
    }

    phys virt_to_phys(void *virt)
    {
	return reinterpret_cast<phys>(virt);
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

    void populate_page(void* addr)
    {
	pt_element pte = processor::read_cr3();
        auto pt = phys_cast<pt_element>(pte_phys(pte));
        auto ptep = &pt[pt_index(addr, nlevels - 1)];
	unsigned level = nlevels - 1;
	while (level > 0) {
	    if (!pte_present(*ptep)) {
		allocate_intermediate_level(ptep);
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

    vma* map_anon_dontzero(uintptr_t start, uintptr_t end, unsigned perm)
    {
        vma* ret = new vma(start, end);
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
}

void page_fault(exception_frame *ef)
{
    auto addr = processor::read_cr2();
    debug(fmt("page fault @ %1$x") % addr);
    abort();
}
