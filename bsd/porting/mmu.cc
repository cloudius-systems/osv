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
#include <osv/pagecache.hh>

void *pmap_mapdev(uint64_t paddr, size_t size)
{
    return (void *)mmio_map(paddr, size);
}

uint64_t virt_to_phys(void *virt)
{
    return mmu::virt_to_phys(virt);
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

void mmu_unmap(void* ab)
{
    pagecache::unmap_arc_buf((arc_buf_t*)ab);
}

void mmu_map(void* key, void* ab, void* page)
{
    pagecache::map_arc_buf((pagecache::hashkey*)key, (arc_buf_t*)ab, page);
}
