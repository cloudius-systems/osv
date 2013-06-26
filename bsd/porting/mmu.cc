#include <errno.h>
#include <stddef.h>
#include <mmu.hh> // OSV
#include "mmu.h"  // Port
#include "mmio.hh"

void *pmap_mapdev(uint64_t paddr, size_t size)
{
    return (void *)mmio_map(paddr, size);
}

uint64_t virt_to_phys(void *virt)
{
    return mmu::virt_to_phys(virt);
}

