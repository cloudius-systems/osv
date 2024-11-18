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
#include <osv/export.h>
#include <osv/kernel_config_memory_jvm_balloon.h>

void *pmap_mapdev(uint64_t paddr, size_t size)
{
    return (void *)mmio_map(paddr, size, "xen_store");
}

uint64_t virt_to_phys(void *virt)
{
    return mmu::virt_to_phys(virt);
}

/*
 * Get the amount of used memory.
 */
OSV_LIBSOLARIS_API
uint64_t kmem_used(void)
{
    return memory::stats::total()
#if CONF_memory_jvm_balloon
- memory::stats::jvm_heap()
#endif
- memory::stats::free();
}

OSV_LIBSOLARIS_API
int vm_paging_needed(void)
{
    return 0;
}

OSV_LIBSOLARIS_API
int vm_throttling_needed(void)
{
    return memory::throttling_needed();
}

OSV_LIBSOLARIS_API
void mmu_unmap(void* ab)
{
    pagecache::unmap_arc_buf((arc_buf_t*)ab);
}

OSV_LIBSOLARIS_API
void mmu_map(void* key, void* ab, void* page)
{
    pagecache::map_arc_buf((pagecache::hashkey*)key, (arc_buf_t*)ab, page);
}
