#ifndef MMU_HH
#define MMU_HH

#include "fs/fs.hh"
#include <stdint.h>
#include <boost/intrusive/set.hpp>

namespace mmu {

    typedef unsigned long ulong;
    typedef uint64_t f_offset;

    enum {
	perm_read = 1,
	perm_write = 2,
	perm_exec = 4,
	perm_rx = perm_read | perm_exec,
	perm_rw = perm_read | perm_write,
	perm_rwx = perm_read | perm_write | perm_exec,
    };

    class vma {
    public:
	vma(void* addr, ulong size);
	void* addr() const;
	ulong size() const;
    private:
	void* _addr;
	ulong _size;
    public:
	boost::intrusive::set_member_hook<> _vma_list_hook;
    };

    vma* map_file(void* addr, size_t size, unsigned perm,
                  file& file, f_offset offset);
    vma* map_anon(void* addr, size_t size, unsigned perm);

}

#endif
