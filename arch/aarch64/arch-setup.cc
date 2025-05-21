/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/drivers_config.h>
#include <osv/kernel_config_logger_debug.h>
#include "arch-setup.hh"
#include <osv/sched.hh>
#include <osv/mempool.hh>
#include <osv/elf.hh>
#include <osv/types.h>
#include <string.h>
#include <osv/boot.hh>
#include <osv/debug.hh>
#include <osv/commands.hh>
#if CONF_drivers_xen
#include <osv/xen.hh>
#endif

#include "arch-mmu.hh"
#include "arch-dtb.hh"
#include "gic-v2.hh"
#include "gic-v3.hh"

#include "drivers/console.hh"
#include "drivers/pl011.hh"
#include "early-console.hh"
#if CONF_drivers_pci
#include <osv/pci.hh>
#endif
#include "drivers/mmio-isa-serial.hh"

#include <alloca.h>

#include <osv/kernel_config_networking_stack.h>

void setup_temporary_phys_map()
{
    // duplicate 1:1 mapping into the lower part of phys_mem
    u64 *pt_ttbr0 = reinterpret_cast<u64*>(processor::read_ttbr0());
    for (auto&& area : mmu::identity_mapped_areas) {
        auto base = reinterpret_cast<void*>(get_mem_area_base(area));
        pt_ttbr0[mmu::pt_index(base, 3)] = pt_ttbr0[0];
    }
    mmu::flush_tlb_all();
}

#if CONF_drivers_pci
void arch_setup_pci()
{
    pci::set_pci_ecam(dtb_get_pci_is_ecam());

    /* linear_map [TTBR0 - PCI config space] */
    u64 pci_cfg;
    size_t pci_cfg_len;
    if (!dtb_get_pci_cfg(&pci_cfg, &pci_cfg_len)) {
        return;
    }

    pci::set_pci_cfg(pci_cfg, pci_cfg_len);
    pci_cfg = pci::get_pci_cfg(&pci_cfg_len);
    mmu::linear_map((void *)pci_cfg, (mmu::phys)pci_cfg, pci_cfg_len,
		    "pci_cfg", mmu::page_size, mmu::mattr::dev);

    /* linear_map [TTBR0 - PCI I/O and memory ranges] */
    u64 ranges[2]; size_t ranges_len[2];
    if (!dtb_get_pci_ranges(ranges, ranges_len, 2)) {
        abort("arch-setup: failed to get PCI ranges.\n");
    }
    pci::set_pci_io(ranges[0], ranges_len[0]);
    pci::set_pci_mem(ranges[1], ranges_len[1]);
    ranges[0] = pci::get_pci_io(&ranges_len[0]);
    ranges[1] = pci::get_pci_mem(&ranges_len[1]);
    mmu::linear_map((void *)ranges[0], (mmu::phys)ranges[0], ranges_len[0],
                    "pci_io", mmu::page_size, mmu::mattr::dev);
    mmu::linear_map((void *)ranges[1], (mmu::phys)ranges[1], ranges_len[1],
                    "pci_mem", mmu::page_size, mmu::mattr::dev);
}
#endif

extern bool opt_pci_disabled;
void arch_setup_free_memory()
{
    setup_temporary_phys_map();

    /* import from loader.cc */
    extern size_t elf_size;
    extern elf::Elf64_Ehdr* elf_header;

    mmu::phys addr = (mmu::phys)elf_header + elf_size;
    mmu::free_initial_memory_range(addr, memory::phys_mem_size);

    /* linear_map [TTBR1] */
    for (auto&& area : mmu::identity_mapped_areas) {
        auto base = reinterpret_cast<void*>(get_mem_area_base(area));
        mmu::linear_map(base + addr, addr, memory::phys_mem_size,
            area == mmu::mem_area::main ? "main" :
            area == mmu::mem_area::page ? "page" : "mempool");
    }

    /* linear_map [TTBR0 - boot, DTB and ELF] */
    /* physical memory layout - relative to the 2MB-aligned address PA stored in mmu::mem_addr
       PA +     0x0 - PA + 0x80000: boot
       PA + 0x80000 - PA + 0x90000: DTB copy
       PA + 0x90000 -       [addr]: kernel ELF */
    mmu::linear_map((void *)(OSV_KERNEL_VM_BASE - 0x80000), (mmu::phys)mmu::mem_addr,
                    addr - mmu::mem_addr, "kernel");

    if (console::PL011_Console::active) {
        /* linear_map [TTBR0 - UART] */
        addr = (mmu::phys)console::aarch64_console.pl011.get_base_addr();
        mmu::linear_map((void *)addr, addr, 0x1000, "pl011", mmu::page_size,
                        mmu::mattr::dev);
    }

#if CONF_drivers_cadence
    if (console::Cadence_Console::active) {
        // linear_map [TTBR0 - UART]
        addr = (mmu::phys)console::aarch64_console.cadence.get_base_addr();
        mmu::linear_map((void *)addr, addr, 0x1000, "cadence", mmu::page_size,
                        mmu::mattr::dev);
    }
#endif

    //Locate GICv2 or GICv3 information in DTB and construct corresponding GIC driver
    u64 dist, redist, cpuif, its, v2m;
    size_t dist_len, redist_len, cpuif_len, its_len, v2m_len;
    if (dtb_get_gic_v3(&dist, &dist_len, &redist, &redist_len, &its, &its_len)) {
        gic::gic = new gic::gic_v3_driver(dist, dist_len, redist, redist_len, its, its_len);
    } else if (dtb_get_gic_v2(&dist, &dist_len, &cpuif, &cpuif_len, &v2m, &v2m_len)) {
        gic::gic = new gic::gic_v2_driver(dist, dist_len, cpuif, cpuif_len, v2m, v2m_len);
    } else {
        abort("arch-setup: failed to get GICv3 nor GiCv2 information from dtb.\n");
    }

#if CONF_drivers_pci
    if (!opt_pci_disabled) {
        arch_setup_pci();
    }
#endif

    // get rid of the command line, before memory is unmapped
    console::mmio_isa_serial_console::clean_cmdline(cmdline);
    osv::parse_cmdline(cmdline);

#if CONF_drivers_mmio
    dtb_collect_parsed_mmio_virtio_devices();
#endif

    mmu::switch_to_runtime_page_tables();

    console::mmio_isa_serial_console::memory_map();
}

void arch_setup_tls(void *tls, const elf::tls_data& info)
{
    struct thread_control_block *tcb;
    memset(tls, 0, sizeof(*tcb) + info.size);

    tcb = (thread_control_block *)tls;
    tcb[0].tls_base = &tcb[1];

    memcpy(&tcb[1], info.start, info.filesize);
    asm volatile ("msr tpidr_el0, %0; msr tpidr_el1, %0; isb; " :: "r"(tcb) : "memory");

    /* check that the tls variable preempt_counter is correct */
    assert(sched::get_preempt_counter() == 1);
}

void arch_init_premain()
{
}

#include "drivers/driver.hh"
#if CONF_drivers_virtio
#include "drivers/virtio.hh"
#include "drivers/virtio-mmio.hh"
#endif
#if CONF_drivers_virtio_rng
#include "drivers/virtio-rng.hh"
#endif
#if CONF_drivers_virtio_blk
#include "drivers/virtio-blk.hh"
#endif
#if CONF_drivers_virtio_scsi
#include "drivers/virtio-scsi.hh"
#endif
#if CONF_networking_stack
#if CONF_drivers_virtio_net
#include "drivers/virtio-net.hh"
#endif
#endif
#if CONF_drivers_virtio_fs
#include "drivers/virtio-fs.hh"
#endif
#if CONF_drivers_nvme
#include "drivers/nvme.hh"
#endif

void arch_init_drivers()
{
    extern boot_time_chart boot_time;

#if CONF_drivers_pci
    if (!opt_pci_disabled) {
        int irqmap_count = dtb_get_pci_irqmap_count();
        if (irqmap_count > 0) {
            u32 mask = dtb_get_pci_irqmask();
            u32 *bdfs = (u32 *)alloca(sizeof(u32) * irqmap_count);
            int *irqs  = (int *)alloca(sizeof(int) * irqmap_count);
            if (!dtb_get_pci_irqmap(bdfs, irqs, irqmap_count)) {
                abort("arch-setup: failed to get PCI irqmap.\n");
            }
            pci::set_pci_irqmap(bdfs, irqs, irqmap_count, mask);
        }

#if CONF_logger_debug
        pci::dump_pci_irqmap();
#endif

        // Enumerate PCI devices
        size_t pci_cfg_len;
        if (pci::get_pci_cfg(&pci_cfg_len)) {
            pci::pci_device_enumeration();
            boot_time.event("pci enumerated");
        }
    }
#endif

#if CONF_drivers_mmio
    // Register any parsed virtio-mmio devices
    virtio::register_mmio_devices(device_manager::instance());
#endif

    // Initialize all drivers
    hw::driver_manager* drvman = hw::driver_manager::instance();
#if CONF_drivers_virtio_rng
    drvman->register_driver(virtio::rng::probe);
#endif
#if CONF_drivers_virtio_blk
    drvman->register_driver(virtio::blk::probe);
#endif
#if CONF_drivers_virtio_scsi
    drvman->register_driver(virtio::scsi::probe);
#endif
#if CONF_networking_stack
#if CONF_drivers_virtio_net
    drvman->register_driver(virtio::net::probe);
#endif
#endif
#if CONF_drivers_virtio_fs
    drvman->register_driver(virtio::fs::probe);
#endif
#if CONF_drivers_nvme
    drvman->register_driver(nvme::driver::probe);
#endif
    boot_time.event("drivers probe");
    drvman->load_all();
    drvman->list_drivers();
}

void arch_init_early_console()
{
    console::mmio_isa_serial_console::_phys_mmio_address = 0;

#if CONF_drivers_xen
    if (is_xen()) {
        new (&console::aarch64_console.xen) console::XEN_Console();
        console::arch_early_console = console::aarch64_console.xen;
        return;
    }
#endif

    int irqid;
    u64 mmio_serial_address = dtb_get_mmio_serial_console(&irqid);
    if (mmio_serial_address) {
        console::mmio_isa_serial_console::early_init(mmio_serial_address);

        new (&console::aarch64_console.isa_serial) console::mmio_isa_serial_console();
        console::aarch64_console.isa_serial.set_irqid(irqid);
        console::arch_early_console = console::aarch64_console.isa_serial;
        return;
    }

#if CONF_drivers_cadence
    mmio_serial_address = dtb_get_cadence_uart(&irqid);
    if (mmio_serial_address) {
        new (&console::aarch64_console.cadence) console::Cadence_Console();
        console::arch_early_console = console::aarch64_console.cadence;
        console::aarch64_console.cadence.set_base_addr(mmio_serial_address);
        console::aarch64_console.cadence.set_irqid(irqid);
        console::Cadence_Console::active = true;
        return;
    }
#endif

    new (&console::aarch64_console.pl011) console::PL011_Console();
    console::arch_early_console = console::aarch64_console.pl011;
    console::PL011_Console::active = true;
    u64 addr = dtb_get_uart(&irqid);
    if (!addr) {
        /* keep using default addresses */
        return;
    }

    console::aarch64_console.pl011.set_base_addr(addr);
    console::aarch64_console.pl011.set_irqid(irqid);
}

bool arch_setup_console(std::string opt_console)
{
    if (opt_console.compare("pl011") == 0) {
        console::console_driver_add(&console::arch_early_console);
    } else if (opt_console.compare("all") == 0) {
        console::console_driver_add(&console::arch_early_console);
    } else {
        return false;
    }
    return true;
}
