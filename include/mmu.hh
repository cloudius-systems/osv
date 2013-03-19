#ifndef MMU_HH
#define MMU_HH

#include "fs/fs.hh"
#include <stdint.h>
#include <boost/intrusive/set.hpp>
#include <osv/types.h>
#include <functional>

namespace mmu {

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
    vma(uintptr_t start, uintptr_t end);
    uintptr_t start() const;
    uintptr_t end() const;
    void* addr() const;
    uintptr_t size() const;
    void split(uintptr_t edge);
    private:
    uintptr_t _start;
    uintptr_t _end;
public:
    boost::intrusive::set_member_hook<> _vma_list_hook;
};

vma* reserve(void* hint, size_t size);
vma* map_file(void* addr, size_t size, unsigned perm,
              file& file, f_offset offset, bool evac);
vma* map_anon(void* addr, size_t size, unsigned perm, bool evac);
void unmap(void* addr, size_t size);
int protect(void *addr, size_t size, unsigned int perm);

typedef uint64_t phys;
phys virt_to_phys(void *virt);
void* phys_to_virt(phys pa);

template <typename T>
T* phys_cast(phys pa)
{
    return static_cast<T*>(phys_to_virt(pa));
}

void linear_map(uintptr_t virt, phys addr, size_t size, size_t slop);
void free_initial_memory_range(uintptr_t addr, size_t size);
void switch_to_runtime_page_table();

}

#endif
