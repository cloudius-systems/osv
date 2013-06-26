#include <errno.h>
#include <stddef.h>
#include <mmu.hh> // OSV
#include "mmu.h"  // Port

void *pmap_mapdev(uint64_t paddr, size_t size)
{
    char* map_to = mmu::phys_mem + paddr;
    mmu::linear_map(map_to, paddr, size, 4096);
    return map_to;
}

uint64_t virt_to_phys(void *virt)
{
    return mmu::virt_to_phys(virt);
}

