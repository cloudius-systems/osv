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
#include "drivers/virtio-mmio.hh"

#define DTB_INTERRUPT_CELLS 3

extern "C" {
#include "libfdt.h"
}

void *dtb;
char *cmdline;

static int dtb_pci_node = -1;

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
    while ((node = fdt_parent_offset(dtb, node)) >= 0) {
        bool status = dtb_getprop_u32(node, name, retval);
        if (status == true) {
            return true;
        }
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

static bool dtb_get_reg_n(int node, u64 *addr, size_t *len, int n)
{
    u32 addr_cells, size_cells;
    memset(addr, 0, sizeof(u64) * n);
    memset(len, 0, sizeof(size_t) * n);

    if (!dtb_getprop_u32_cascade(node, "#address-cells", &addr_cells))
        return false;

    if (!dtb_getprop_u32_cascade(node, "#size-cells", &size_cells))
        return false;

    int size;
    u32 *reg = (u32 *)fdt_getprop(dtb, node, "reg", &size);
    int required = ((addr_cells + size_cells) * sizeof(u32)) * n;

    if (!reg || size < required)
        return false;

    for (int x = 0; x < n; x++) {
        for (u32 i = 0; i < addr_cells; i++, reg++) {
            addr[x] = addr[x] << 32 | fdt32_to_cpu(*reg);
        }
        for (u32 i = 0; i < size_cells; i++, reg++) {
            len[x] = len[x] << 32 | fdt32_to_cpu(*reg);
        }
    }

    return true;
}

static bool dtb_get_int_spec(int node, struct dtb_int_spec *s, int n)
{
    int size;
    u32 *ints = (u32 *)fdt_getprop(dtb, node, "interrupts", &size);
    int required = (DTB_INTERRUPT_CELLS * n) * sizeof(u32);

    if (!ints || size < required)
        return false;

    for (int i = 0; i < n; i++, ints += DTB_INTERRUPT_CELLS) {
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

u64 dtb_get_uart(int *irqid)
{
    u64 retval;
    int node; size_t len;

    if (!dtb)
        return 0;

    node = fdt_node_offset_by_compatible(dtb, -1, "arm,pl011");
    if (node < 0)
        return 0;

    len = dtb_get_reg(node, &retval);
    if (!len)
        return 0;

    struct dtb_int_spec int_spec[1];
    if (!dtb_get_int_spec(node, int_spec, 1))
        return 0;

    *irqid = int_spec[0].irq_id;
    return retval;
}

u64 dtb_get_rtc()
{
    u64 retval;
    int node; size_t len;

    if (!dtb)
        return 0;

    node = fdt_node_offset_by_compatible(dtb, -1, "arm,pl031");
    if (node < 0)
        return 0;

    len = dtb_get_reg(node, &retval);
    if (!len)
        return 0;

    return retval;
}

u64 dtb_get_mmio_serial_console(int *irqid)
{
    int node;
    struct dtb_int_spec int_spec[1];
    u64 address;

    if (!dtb)
        return 0;

    node = fdt_node_offset_by_compatible(dtb, -1, "ns16550a");
    if (node < 0)
        return 0;

    const char *node_name = fdt_get_name(dtb, node, NULL);
    if (!node_name) {
        return 0;
    }

    if (sscanf(node_name,"uart@%x", &address) != 1) {
        return 0;
    }

    if( !dtb_get_int_spec(node, int_spec, 1)) {
        return 0;
    };

    *irqid = int_spec[0].irq_id;
    return address;
}

u64 dtb_get_cadence_uart(int *irqid)
{
    const char *compatible[] = {
            "cdns,uart-r1p8",
            "cdns,uart-r1p12",
            "xlnx,xuartps",
    };
    unsigned int i;
    int node;
    struct dtb_int_spec int_spec[1];
    u64 addr;

    if (!dtb) {
        return 0;
    }

    for (i = 0; i < sizeof(compatible)/sizeof(compatible[0]); i++) {
        node = fdt_node_offset_by_compatible(dtb, -1, compatible[i]);
        if (node >= 0)
            break;
    }

    if (node < 0) {
        return 0;
    }

    if (!dtb_get_reg(node, &addr)) {
        return 0;
    }

    if (!dtb_get_int_spec(node, int_spec, 1)) {
        return 0;
    }

    if (irqid) {
        *irqid = int_spec[0].irq_id;
    }
    return addr;
}

#define VIRTIO_MMIO_DEV_COMPAT "virtio,mmio"
#define DTB_MAX_VIRTIO_MMIO_DEV_COUNT 8
static virtio::mmio_device_info dtb_dtb_virtio_mmio_devices_infos[DTB_MAX_VIRTIO_MMIO_DEV_COUNT];
static unsigned int dtb_virtio_mmio_dev_count;
static void dtb_parse_mmio_virtio_devices()
{
    int node;
    struct dtb_int_spec int_spec[1];

    if (!dtb)
        return;

    dtb_virtio_mmio_dev_count = 0;
    node = fdt_node_offset_by_compatible(dtb, -1, VIRTIO_MMIO_DEV_COMPAT);
    while (node != -FDT_ERR_NOTFOUND && dtb_virtio_mmio_dev_count < DTB_MAX_VIRTIO_MMIO_DEV_COUNT) {
	int value_size;
        int required = 2 * sizeof(u64); // We expect two fields - address and length, each u64 size
        u64 *reg = (u64 *)fdt_getprop(dtb, node, "reg", &value_size);
        if (!reg || value_size < required) {
	    break;
	}

	dtb_dtb_virtio_mmio_devices_infos[dtb_virtio_mmio_dev_count]._address = fdt64_to_cpu(reg[0]);
	dtb_dtb_virtio_mmio_devices_infos[dtb_virtio_mmio_dev_count]._size = fdt64_to_cpu(reg[1]);

        if( !dtb_get_int_spec(node, int_spec, 1)) {
            break;
        };

        dtb_dtb_virtio_mmio_devices_infos[dtb_virtio_mmio_dev_count++]._irq_no = int_spec[0].irq_id;

	// Move to the next node
	node = fdt_node_offset_by_compatible(dtb, node, VIRTIO_MMIO_DEV_COMPAT);
    }
}

void dtb_collect_parsed_mmio_virtio_devices()
{
    for( unsigned int idx = 0; idx < dtb_virtio_mmio_dev_count; idx++) {
	virtio::add_mmio_device_configuration(dtb_dtb_virtio_mmio_devices_infos[idx]);
    }
}

/* this gets the virtual timer irq, we are not interested
 * about the other timers.
 */
static int dtb_timer_irq = -1;
static int dtb_parse_timer_irq()
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

int dtb_get_timer_irq()
{
    return dtb_timer_irq;
}

/* this gets the GIC distributor and cpu interface addresses */
bool dtb_get_gic_v2(u64 *dist, size_t *dist_len, u64 *cpu, size_t *cpu_len)
{
    u64 addr[2], len[2];
    int node;

    if (!dtb)
        return false;

    node = fdt_node_offset_by_compatible(dtb, -1, "arm,gic-400");
    if (node < 0) {
        node = fdt_node_offset_by_compatible(dtb, -1, "arm,cortex-a15-gic");
        if (node < 0)
            return false;
    }

    if (!dtb_get_reg_n(node, addr, len, 2))
        return false;

    *dist = addr[0];
    *dist_len = len[0];
    *cpu = addr[1];
    *cpu_len = len[1];

    return true;
}

/* this gets the cpus node and returns the number of cpu elements in it. */
static int dtb_cpu_count = -1;
static int dtb_parse_cpus_count()
{
    int node, subnode, count;
    if (!dtb)
        return -1;

    node = fdt_path_offset(dtb, "/cpus");
    if (node < 0)
        return -1;

    for (count = 0, subnode = fdt_first_subnode(dtb, node);
         subnode >= 0;
         count++,   subnode = fdt_next_subnode(dtb, subnode)) {
    }
    return count;
}

int dtb_get_cpus_count()
{
    return dtb_cpu_count;
}

/* this gets the cpu mpidr values for all cpus */
#define DTB_MAX_CPU_COUNT 32
static u64 dtb_cpus_mpids[DTB_MAX_CPU_COUNT];
bool dtb_parse_cpus_mpid(u64 *mpids, int n)
{
    int node, subnode;

    if (n > DTB_MAX_CPU_COUNT) {
        abort("dtb_parse_cpus_mpid: number of cpus greater than maximum. Increase the DTB_MAX_CPU_COUNT!\n");
    }

    if (!dtb)
        return false;

    node = fdt_path_offset(dtb, "/cpus");
    if (node < 0)
        return false;

    for (subnode = fdt_first_subnode(dtb, node);
         n > 0 && subnode >= 0;
         subnode = fdt_next_subnode(dtb, subnode), n--, mpids++) {

        (void)dtb_get_reg(subnode, mpids);
    }
    return true;
}

bool dtb_get_cpus_mpid(u64 *mpids, int n) {
    for (auto i = 0; i < n; i++) {
        mpids[i] = dtb_cpus_mpids[i];
    }
    return true;
}

static int dtb_get_pci_node()
{
    if (dtb_pci_node >= 0) {
        return dtb_pci_node;
    }

    if (!dtb) {
        abort("dtb_get_pci_node: dtb == NULL\n");
    }

    dtb_pci_node = fdt_node_offset_by_compatible(dtb, -1, "pci-host-ecam-generic");
    if (dtb_pci_node >= 0) {
        goto out;
    }

    dtb_pci_node = fdt_node_offset_by_compatible(dtb, -1, "pci-host-cam-generic");
    if (dtb_pci_node >= 0) {
        goto out;
    }

 out:
    return dtb_pci_node;
}

bool dtb_get_pci_is_ecam()
{
    int offset = dtb_get_pci_node();
    return !fdt_node_check_compatible(dtb, offset, "pci-host-ecam-generic");
}

/* this gets the PCI configuration space base address */
bool dtb_get_pci_cfg(u64 *addr, size_t *len)
{
    int node;

    if (!dtb)
        return false;

    node = dtb_get_pci_node();
    *len = dtb_get_reg(node, addr);
    return *len > 0;
}

/* just get the CPU memory areas for now */
bool dtb_get_pci_ranges(u64 *addr, size_t *len, int n)
{
    u32 addr_cells_pci, addr_cells, size_cells;
    int node;

    memset(addr, 0, sizeof(u64) * n);
    memset(len, 0, sizeof(size_t) * n);

    if (!dtb)
        return false;

    node = dtb_get_pci_node();

    if (!dtb_getprop_u32(node, "#address-cells", &addr_cells_pci))
        return false;

    if (!dtb_getprop_u32_cascade(node, "#address-cells", &addr_cells))
        return false;

    if (!dtb_getprop_u32_cascade(node, "#size-cells", &size_cells))
        return false;

    int size;
    u32 *ranges = (u32 *)fdt_getprop(dtb, node, "ranges", &size);
    int required = (addr_cells + size_cells) * sizeof(u32) * n;

    if (!ranges || size < required)
        return false;

    for (int x = 0; x < n; x++) {
        for (u32 i = 0; i < addr_cells_pci; i++, ranges++) {
            /* ignore the PCI address */
        }
        for (u32 i = 0; i < addr_cells; i++, ranges++) {
            addr[x] = addr[x] << 32 | fdt32_to_cpu(*ranges);
        }
        for (u32 i = 0; i < size_cells; i++, ranges++) {
            len[x] = len[x] << 32 | fdt32_to_cpu(*ranges);
        }
    }

    return true;
}

/* Interrupt Mapping spec version 0.9
 *
 * http://www.firmware.org/1275/practice/imap/imap0_9d.pdf
 * (good luck reading that mess)
 */

static int dtb_get_pua_cells(u32 phandle)
{
    u32 retval;

    if (!dtb)
        return -1;

    int node = fdt_node_offset_by_phandle(dtb, phandle);
    if (node < 0) {
        return -1;
    }
    if (!dtb_getprop_u32(node, "#address-cells", &retval)) {
        /* if it's not there, it is assumed to be empty, see spec */
        retval = 0;
    }
    return retval;
}

/* get the number of mappings between pci devices and platform IRQs. */
static int dtb_pci_irqmap_count = -1;
static int dtb_parse_pci_irqmap_count()
{
    int count;
    if (!dtb)
        return -1;

    int size, node = dtb_get_pci_node();
    u32 *prop = (u32 *)fdt_getprop(dtb, node, "interrupt-map", &size);

    if (!prop) {
        return -1;
    }

    int cells = size / sizeof(u32);

    for (count = 0; cells > 0; count++) {
        if (cells < 5) {
            return -1;
        }
        /* skip */
        prop += 4;
        /* get the parent gic phandle */
        u32 phandle = fdt32_to_cpu(prop[0]);
        prop += 1;
        cells -= 5;
        /* ignore parent address */
        int pua_cells = dtb_get_pua_cells(phandle);
        if (pua_cells < 0) {
            return -1;
        }
        if (cells < pua_cells + DTB_INTERRUPT_CELLS) {
            return -1;
        }
        prop += pua_cells + DTB_INTERRUPT_CELLS;
        cells -= pua_cells + DTB_INTERRUPT_CELLS;
    }
    return count;
}

int dtb_get_pci_irqmap_count()
{
    return dtb_pci_irqmap_count;
}

/* gets the mask for just the slot member of the pci address. */
static int dtb_pci_irqmask = -1;
u32 dtb_parse_pci_irqmask()
{
    u32 *prop;
    int node, size;

    if (!dtb)
        return 0;

    node = dtb_get_pci_node();
    prop = (u32 *)fdt_getprop(dtb, node, "interrupt-map-mask", &size);

    /* we require each entry to be 16 bytes, as PCI unit interrupt specifier
     * always consists of 4 cells,
     * 3 for the unit address and 1 for the interrupt specifier.
     */

    if (size != 16)
        return 0;

    /* see irqmap below */
    return (fdt32_to_cpu(prop[0]) & DTB_PHYSHI_BDF_MASK) | DTB_PIN_MASK;
}

u32 dtb_get_pci_irqmask()
{
    return dtb_pci_irqmask;
}

#define DTB_MAX_IRQ_COUNT 32
static u32 dtb_pci_bdfs[DTB_MAX_IRQ_COUNT];
static int dtb_pci_irq_ids[DTB_MAX_IRQ_COUNT];
static bool dtb_parse_pci_irqmap(u32 *bdfs, int *irq_ids, int n)
{
    if (n > DTB_MAX_IRQ_COUNT) {
        abort("dtb_parse_pci_irqmap: number of iqrs greater than maximum. Increase the DTB_MAX_IRQ_COUNT!\n");
    }

    if (!dtb)
        return false;

    int size, node = dtb_get_pci_node();
    u32 *prop = (u32 *)fdt_getprop(dtb, node, "interrupt-map", &size);

    if (!prop) {
        return false;
    }

    int cells = size / sizeof(u32);

    for (int i = 0; i < n; i++) {
        if (cells < 5) {
            return false;
        }
        /* get the BDF part of PHYS.HI */
        bdfs[i] = fdt32_to_cpu(prop[0]) & DTB_PHYSHI_BDF_MASK;
        prop += 1;
        /* ignore PHYS.mid/low */
        prop += 2;
        /* get the PCI interrupt pin */
        bdfs[i] |= fdt32_to_cpu(prop[0]) & DTB_PIN_MASK;
        prop += 1;
        /* get the parent gic phandle */
        u32 phandle = fdt32_to_cpu(prop[0]);
        prop += 1;
        cells -= 5;

        /* ignore parent address */
        int pua_cells = dtb_get_pua_cells(phandle);
        if (pua_cells < 0) {
            return false;
        }
        if (cells < pua_cells + DTB_INTERRUPT_CELLS) {
            return false;
        }
        prop += pua_cells;
        /* ignore interrupt type */
        prop += 1;
        /* get interrupt value */
        irq_ids[i] = fdt32_to_cpu(prop[0]);
        prop += 1;
        /* ignore interrupt flags */
        prop += 1;
        size -= pua_cells * DTB_INTERRUPT_CELLS;
    }
    return true;
}

bool dtb_get_pci_irqmap(u32 *bdfs, int *irq_ids, int n)
{
    for (auto i = 0; i < n; i++) {
        bdfs[i] = dtb_pci_bdfs[i];
        irq_ids[i] = dtb_pci_irq_ids[i];
    }
    return true;
}

bool dtb_get_vmm_is_xen()
{
    if (fdt_check_header(dtb) != 0)
        return false; /* broken header will be handled later */

    return fdt_node_offset_by_compatible(dtb, -1, "xen,xen") >= 0;
}

const char *dtb_get_psci_method()
{
    const char *psci_compatible[] = {
            "arm,psci",
            "arm,psci-0.2",
            "arm,psci-1.0",
    };
    unsigned int i;
    int node;

    for (i = 0; i < sizeof(psci_compatible)/sizeof(psci_compatible[0]); i++) {
        node = fdt_node_offset_by_compatible(dtb, -1, psci_compatible[i]);
        if (node >= 0)
            break;
    }

    if (node < 0) {
        return NULL;
    }

    const char *method = (char *)fdt_getprop(dtb, node, "method", NULL);
    if (!method) {
        return NULL;
    }

    return method;
}

void  __attribute__((constructor(init_prio::dtb))) dtb_setup()
{
    void *olddtb;
    int node;
    char *cmdline_override;
    int len;

    if (fdt_check_header(dtb) != 0) {
        abort("dtb_setup: device tree blob invalid.\n");
    }

    memory::phys_mem_size = dtb_get_phys_memory(&mmu::mem_addr);
    if (!memory::phys_mem_size) {
        abort("dtb_setup: failed to parse memory information.\n");
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
        abort("dtb_setup: failed to add node /chosen for cmdline.\n");
    }

    cmdline_override = (char *)fdt_getprop(dtb, node, "bootargs", &len);
    if (cmdline_override) {
        cmdline = cmdline_override;
    } else {
        len = strlen(cmdline) + 1;
        if (fdt_setprop(dtb, node, "bootargs", cmdline, len) < 0) {
            abort("dtb_setup: failed to set bootargs in /chosen.\n");
        }
    }
    if ((size_t)len > max_cmdline) {
        abort("dtb_setup: command line too long.\n");
    }

    if (fdt_pack(dtb) != 0) {
        abort("dtb_setup: failed to pack dtb\n");
    }

    olddtb = dtb;
    dtb = (void *)OSV_KERNEL_BASE;

    if (fdt_open_into(olddtb, dtb, 0x10000) != 0) {
        abort("dtb_setup: failed to move dtb (dtb too large?)\n");
    }

    cmdline = (char *)fdt_getprop(dtb, node, "bootargs", NULL);
    if (!cmdline) {
        abort("dtb_setup: cannot find cmdline after dtb move.\n");
    }
    // Parse some dtb configuration ahead of time
    dtb_cpu_count = dtb_parse_cpus_count();
    if (!dtb_parse_cpus_mpid(dtb_cpus_mpids, dtb_cpu_count)) {
        abort("dtb_setup: failed to parse cpu mpid.\n");
    }

    dtb_timer_irq = dtb_parse_timer_irq();
    dtb_pci_irqmask = dtb_parse_pci_irqmask();
    dtb_pci_irqmap_count = dtb_parse_pci_irqmap_count();
    if (dtb_pci_irqmap_count > 0 && !dtb_parse_pci_irqmap(dtb_pci_bdfs, dtb_pci_irq_ids, dtb_pci_irqmap_count)) {
        abort("dtb_setup: failed to parse pci_irq_map.\n");
    }

    dtb_parse_mmio_virtio_devices();

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
