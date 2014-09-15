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

struct dtb_int_spec {
    int irq_id;
    unsigned int flags;
};

static bool dtb_getprop_u32(int node, const char *name, u32 *retval)
{
    u32 *prop;
    int size;

    prop = (u32 *)fdt_getprop(dtb, node, name, &size);
    if (!prop || size != 4) {
        return false;
    } else {
        *retval = fdt32_to_cpu(*prop);
        return true;
    }
}

static bool dtb_getprop_u32_cascade(int node, const char *name, u32 *retval)
{
    while (node >= 0) {
        bool status = dtb_getprop_u32(node, name, retval);
        if (status == true) {
            return true;
        }
        node = fdt_parent_offset(dtb, node);
    }
    return false;
}

static size_t dtb_get_reg(int node, u64 *addr)
{
    u32 addr_cells, size_cells;
    *addr = 0;

    if (!dtb_getprop_u32_cascade(node, "#address-cells", &addr_cells))
        return 0;

    if (!dtb_getprop_u32_cascade(node, "#size-cells", &size_cells))
        return 0;

    int size;
    u32 *reg = (u32 *)fdt_getprop(dtb, node, "reg", &size);
    int required = (addr_cells + size_cells) * sizeof(u32);

    if (!reg || size < required)
        return 0;

    for (u32 i = 0; i < addr_cells; i++, reg++) {
        *addr = *addr << 32 | fdt32_to_cpu(*reg);
    }

    size_t retval = 0;
    for (u32 i = 0; i < size_cells; i++, reg++) {
        retval = retval << 32 | fdt32_to_cpu(*reg);
    }

    return retval;
}

/* NB: assumes #interrupt-cells = 3 */
static bool dtb_get_int_spec(int node, struct dtb_int_spec *s, int n)
{
    int size;
    u32 *ints = (u32 *)fdt_getprop(dtb, node, "interrupts", &size);
    int required = (3 * n) * sizeof(u32);

    if (!ints || size < required)
        return false;

    for (int i = 0; i < n; i++, ints += 3) {
        u32 value = fdt32_to_cpu(ints[0]);
        switch (value) {
        case 0:
            s[i].irq_id = 32;
            break;
        case 1:
            s[i].irq_id = 16;
            break;
        default:
            return false;
        }
        value = fdt32_to_cpu(ints[1]);
        s[i].irq_id += value;
        value = fdt32_to_cpu(ints[2]);
        s[i].flags = value;
    }

    return true;
}

void  __attribute__((constructor(init_prio::dtb))) dtb_setup()
{
    void *olddtb;
    int node;
    char *cmdline_override;
    int len;

    if (fdt_check_header(dtb) != 0) {
        debug_early("dtb_setup: device tree blob invalid, using defaults.\n");
        goto out_err_def_mem;
    }

    memory::phys_mem_size = dtb_get_phys_memory(&mmu::mem_addr);
    if (!memory::phys_mem_size) {
        debug_early("dtb_setup: failed to parse memory information.\n");
        goto out_err_def_mem;
    }

    /* command line will be overwritten with DTB: move it inside DTB */

    node = fdt_path_offset(dtb, "/chosen");
    if (node < 0) {
        node = fdt_path_offset(dtb, "/");
        if (node >= 0) {
            node = fdt_add_subnode(dtb, node, "chosen");
        }
    }
    if (node < 0) {
        debug_early("dtb_setup: failed to add node /chosen for cmdline.\n");
        goto out_err_dtb;
    }

    cmdline_override = (char *)fdt_getprop(dtb, node, "bootargs", &len);
    if (cmdline_override) {
        cmdline = cmdline_override;
    } else {
        len = strlen(cmdline) + 1;
        if (fdt_setprop(dtb, node, "bootargs", cmdline, len) < 0) {
            debug_early("dtb_setup: failed to set bootargs in /chosen.\n");
            goto out_err_dtb;
        }
    }
    if ((size_t)len > max_cmdline) {
        abort("dtb_setup: command line too long.\n");
    }
    olddtb = dtb;
    dtb = (void *)OSV_KERNEL_BASE;

    if (fdt_move(olddtb, dtb, 0x10000) != 0) {
        debug_early("dtb_setup: failed to move dtb (dtb too large?)\n");
        goto out_err_dtb;
    }

    cmdline = (char *)fdt_getprop(dtb, node, "bootargs", NULL);
    if (!cmdline) {
        abort("dtb_setup: cannot find cmdline after dtb move.\n");
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

size_t dtb_get_phys_memory(u64 *addr)
{
    size_t retval;
    int node;

    if (!dtb)
        return 0;

    node = fdt_path_offset(dtb, "/memory");
    if (node < 0)
        return 0;

    retval = dtb_get_reg(node, addr);
    return retval;
}

u64 dtb_get_uart_base()
{
    u64 retval;
    int node; size_t len __attribute__((unused));

    if (!dtb)
        return 0;

    node = fdt_node_offset_by_compatible(dtb, -1, "pl011");
    if (node < 0)
        return 0;

    len = dtb_get_reg(node, &retval);
    return retval;
}

/* this gets the virtual timer irq, we are not interested
 * about the other timers.
 */

int dtb_get_timer_irq()
{
    int node;
    struct dtb_int_spec int_spec[4];

    if (!dtb)
        return 0;

    node = fdt_path_offset(dtb, "/timer");
    if (node < 0)
        return 0;

    if (!dtb_get_int_spec(node, int_spec, 4))
        return 0;

    return int_spec[2].irq_id;
}
