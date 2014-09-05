/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <osv/debug.h>
#include <stdlib.h>
#include <osv/prio.hh>

#include "arch-dtb.hh"

#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/commands.hh>
#include <osv/elf.hh>

extern "C" {
#include "libfdt.h"
}

void *dtb;
char *cmdline;

void  __attribute__((constructor(init_prio::dtb))) dtb_setup()
{
    void *olddtb;
    osv::parse_cmdline(cmdline); /* will be overwritten with dtb */

    if (fdt_check_header(dtb) != 0) {
        debug_early("dtb_setup: device tree blob invalid, using defaults.\n");
        goto out_err_def_mem;
    }

    memory::phys_mem_size = dtb_get_phys_memory(&mmu::mem_addr);
    if (!memory::phys_mem_size) {
        debug_early("dtb_setup: failed to parse memory information.\n");
        goto out_err_def_mem;
    }

    olddtb = dtb;
    dtb = (void *)OSV_KERNEL_BASE;

    if (fdt_move(olddtb, dtb, 0x10000) != 0) {
        debug_early("dtb_setup: failed to move dtb (dtb too large?)\n");
        goto out_err_dtb;
    }

    goto out_ok; /* success */

 out_err_def_mem:
    memory::phys_mem_size = 0x20000000; /* 512 MB memory */
    mmu::mem_addr = 0x40000000;
 out_err_dtb:
    dtb = NULL;
 out_ok:
    register u64 edata;
    asm volatile ("adrp %0, .edata" : "=r"(edata));

    /* import from loader.cc */
    extern elf::Elf64_Ehdr *elf_header;
    extern size_t elf_size;
    extern void *elf_start;

    elf_start = reinterpret_cast<void *>(elf_header);
    elf_size = (u64)edata - (u64)elf_start;

    /* remove amount of memory used for ELF from avail memory */
    mmu::phys addr = (mmu::phys)elf_start + elf_size;
    memory::phys_mem_size -= addr - mmu::mem_addr;
}

static u32 dtb_getprop_u32(int node, const char *name, int *lenp)
{
    u32 retval, *prop;
    prop = (u32 *)fdt_getprop(dtb, node, name, lenp);
    if (!prop) {
        retval = *lenp = 0;
    } else {
        retval = fdt32_to_cpu(*prop);
    }
    return retval;
}

size_t dtb_get_phys_memory(u64 *addr)
{
    size_t retval;
    int node, size;
    u32 addr_cells, size_cells;

    if (!dtb)
        return 0;

    node = fdt_path_offset(dtb, "/");
    if (node < 0)
        return 0;

    addr_cells = dtb_getprop_u32(node, "#address-cells", &size);
    if (!addr_cells || (size != 4))
        return 0;

    size_cells = dtb_getprop_u32(node, "#size-cells", &size);
    if (!size_cells || (size != 4))
        return 0;

    node = fdt_path_offset(dtb, "/memory");
    if (node < 0)
        return 0;

    u32 *reg = (u32 *)fdt_getprop(dtb, node, "reg", &size);
    int required = (addr_cells + size_cells) * sizeof(u32);

    if (!reg || size < required)
        return 0;

    *addr = 0;
    for (u32 i = 0; i < addr_cells; i++, reg++) {
        *addr = *addr << 32 | fdt32_to_cpu(*reg);
    }

    retval = 0;
    for (u32 i = 0; i < size_cells; i++, reg++) {
        retval = retval << 32 | fdt32_to_cpu(*reg);
    }

    return retval;
}
