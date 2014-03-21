/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_BSD_MMU_H
#define _OSV_BSD_MMU_H

#include <sys/cdefs.h>
#include <sys/types.h>

typedef void *vm_page_t;
typedef uint64_t vm_paddr_t;
typedef uint64_t vm_offset_t;

/*
 * Page-directory and page-table entries follow this format, with a few
 * of the fields not present here and there, depending on a lot of things.
 */
				/* ---- Intel Nomenclature ---- */
#define	PG_V		0x001	/* P	Valid			*/
#define PG_RW		0x002	/* R/W	Read/Write		*/
#define PG_U		0x004	/* U/S  User/Supervisor		*/
#define	PG_NC_PWT	0x008	/* PWT	Write through		*/
#define	PG_NC_PCD	0x010	/* PCD	Cache disable		*/
#define PG_A		0x020	/* A	Accessed		*/
#define	PG_M		0x040	/* D	Dirty			*/
#define	PG_PS		0x080	/* PS	Page size (0=4k,1=2M)	*/
#define	PG_PTE_PAT	0x080	/* PAT	PAT index		*/
#define	PG_G		0x100	/* G	Global			*/
#define	PG_AVAIL1	0x200	/*    /	Available for system	*/
#define	PG_AVAIL2	0x400	/*   <	programmers use		*/
#define	PG_AVAIL3	0x800	/*    \				*/
#define	PG_PDE_PAT	0x1000	/* PAT	PAT index		*/
#define	PG_NX		(1ul<<63) /* No-execute */

__BEGIN_DECLS
void *pmap_mapdev(uint64_t addr, size_t size);
uint64_t virt_to_phys(void *virt);
static inline vm_paddr_t pmap_kextract(vm_offset_t va)
{
    // In BSD this depends on the type of the address, but maybe
    // that will do for now
    return virt_to_phys((void *)va);
}

uint64_t kmem_used(void);
int vm_paging_needed(void);

void mmu_unmap(void *addr, size_t size);

#define vtophys(_va) virt_to_phys((void *)_va)
__END_DECLS
#endif
