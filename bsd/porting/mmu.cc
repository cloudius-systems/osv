/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <errno.h>
#include <stddef.h>
#include <osv/mmu.hh> // OSV
#include "mmu.h"  // Port
#include <osv/mmio.hh>
#include <osv/mempool.hh>

void *pmap_mapdev(uint64_t paddr, size_t size)
{
    return (void *)mmio_map(paddr, size);
}

uint64_t virt_to_phys(void *virt)
{
#ifdef AARCH64_PORT_STUB
    return (uint64_t)virt;
#else /* !AARCH64_PORT_STUB */
    return mmu::virt_to_phys(virt);
#endif /* !AARCH64_PORT_STUB */
}

/*
 * Get the amount of used memory.
 */
uint64_t kmem_used(void)
{
    return memory::stats::total() - memory::stats::jvm_heap() - memory::stats::free();
}

int vm_paging_needed(void)
{
    return 0;
}

int vm_throttling_needed(void)
{
    return memory::throttling_needed();
}

void mmu_unmap(void *addr, size_t size)
{
#ifdef AARCH64_PORT_STUB
    abort();
#else
    mmu::unmap_address(addr, addr, size);
#endif /* !AARCH64_PORT_STUB */
}

#ifndef AARCH64_PORT_STUB
namespace mmu {
extern mutex vma_list_mutex;
}
#endif /* !AARCH64_PORT_STUB */

bool mmu_vma_list_trylock()
{
#ifdef AARCH64_PORT_STUB
    abort();
#else  /* !AARCH64_PORT_STUB */
    return mutex_trylock(&mmu::vma_list_mutex);
#endif /* !AARCH64_PORT_STUB */
}

void mmu_vma_list_unlock()
{
#ifdef AARCH64_PORT_STUB
    abort();
#else  /* !AARCH64_PORT_STUB */
    mutex_unlock(&mmu::vma_list_mutex);
#endif /* !AARCH64_PORT_STUB */
}
