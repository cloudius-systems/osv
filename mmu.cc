#include "mmu.hh"
#include "arch/x64/processor.hh"
#include "drivers/console.hh"
#include "exceptions.hh"
#include <boost/format.hpp>

extern Console* debug_console;

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
    typedef uint64_t phys;
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
	// FIXME: allocate pages, not bytes; wastefull
	void *p = malloc(8191);
	auto ret = virt_to_phys(p);
	ret = (ret + 4095) & ~phys(4095);
	return ret;
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
	pt_element pte = processor::x86::read_cr3();
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

    vma* map_anon(void* addr, size_t size, unsigned perm)
    {
        vma* ret = new vma(addr, size);
        vma_list.insert(*ret);

        populate(*ret);

        return ret;
    }

    vma* map_file(void* addr, size_t size, unsigned perm,
                  file& f, f_offset offset)
    {
        vma* ret = map_anon(addr, size, perm);
        f.read(addr, offset, size);
        return ret;
    }

    vma::vma(void* addr, ulong size)
        : _addr(addr)
        , _size(size)
    {
    }

    void* vma::addr() const
    {
        return _addr;
    }

    ulong vma::size() const
    {
        return _size;
    }
}

void page_fault(exception_frame *ef)
{
    auto addr = processor::x86::read_cr2();
    debug_console->writeln(fmt("page fault @ %1$x") % addr);
    abort();
}
