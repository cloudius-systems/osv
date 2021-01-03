/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch-setup.hh"
#include <osv/sched.hh>
#include <osv/mempool.hh>
#include <osv/elf.hh>
#include <osv/types.h>
#include <string.h>
#include <osv/boot.hh>
#include <osv/debug.hh>
#include <osv/commands.hh>
#include <osv/xen.hh>

#include "arch-mmu.hh"
#include "arch-dtb.hh"

#include "drivers/console.hh"
#include "drivers/pl011.hh"
#include "early-console.hh"
#include <osv/pci.hh>
#include "drivers/mmio-isa-serial.hh"

#include <alloca.h>

void setup_temporary_phys_map()
{
    // duplicate 1:1 mapping into phys_mem
    u64 *pt_ttbr0 = reinterpret_cast<u64*>(processor::read_ttbr0());
    u64 *pt_ttbr1 = reinterpret_cast<u64*>(processor::read_ttbr1());
    for (auto&& area : mmu::identity_mapped_areas) {
        auto base = reinterpret_cast<void*>(get_mem_area_base(area));
        pt_ttbr1[mmu::pt_index(base, 3)] = pt_ttbr0[0];
    }
    mmu::flush_tlb_all();
}

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
		    mmu::page_size, mmu::mattr::dev);

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
                    mmu::page_size, mmu::mattr::dev);
    mmu::linear_map((void *)ranges[1], (mmu::phys)ranges[1], ranges_len[1],
                    mmu::page_size, mmu::mattr::dev);
}

void arch_setup_free_memory()
{
    setup_temporary_phys_map();

    /* import from loader.cc */
    extern size_t elf_size;
    extern void *elf_start;

    mmu::phys addr = (mmu::phys)elf_start + elf_size;
    mmu::free_initial_memory_range(addr, memory::phys_mem_size);

    /* linear_map [TTBR1] */
    for (auto&& area : mmu::identity_mapped_areas) {
        auto base = reinterpret_cast<void*>(get_mem_area_base(area));
        mmu::linear_map(base + addr, addr, memory::phys_mem_size);
    }

    /* linear_map [TTBR0 - boot, DTB and ELF] */
    mmu::linear_map((void *)mmu::mem_addr, (mmu::phys)mmu::mem_addr,
                    addr - mmu::mem_addr);

    if (console::PL011_Console::active) {
        /* linear_map [TTBR0 - UART] */
        addr = (mmu::phys)console::aarch64_console.pl011.get_base_addr();
        mmu::linear_map((void *)addr, addr, 0x1000, mmu::page_size,
                        mmu::mattr::dev);
    }

    /* linear_map [TTBR0 - GIC DIST and GIC CPU] */
    u64 dist, cpu;
    size_t dist_len, cpu_len;
    if (!dtb_get_gic_v2(&dist, &dist_len, &cpu, &cpu_len)) {
        abort("arch-setup: failed to get GICv2 information from dtb.\n");
    }
    gic::gic = new gic::gic_driver(dist, cpu);
    mmu::linear_map((void *)dist, (mmu::phys)dist, dist_len, mmu::page_size,
                    mmu::mattr::dev);
    mmu::linear_map((void *)cpu, (mmu::phys)cpu, cpu_len, mmu::page_size,
                    mmu::mattr::dev);

    arch_setup_pci();

    // get rid of the command line, before memory is unmapped
    console::mmio_isa_serial_console::clean_cmdline(cmdline);
    osv::parse_cmdline(cmdline);

    dtb_collect_parsed_mmio_virtio_devices();

    mmu::switch_to_runtime_page_tables();

    console::mmio_isa_serial_console::memory_map();
}

void arch_setup_tls(void *tls, const elf::tls_data& info)
{
    struct thread_control_block *tcb;
    memset(tls, 0, info.size + 1024);

    tcb = (thread_control_block *)tls;
    tcb[0].tls_base = &tcb[1];

    memcpy(&tcb[1], info.start, info.filesize);
    asm volatile ("msr tpidr_el0, %0; isb; " :: "r"(tcb) : "memory");

    /* check that the tls variable preempt_counter is correct */
    assert(sched::get_preempt_counter() == 1);
}

void arch_init_premain()
{
}

#include "drivers/driver.hh"
#include "drivers/virtio.hh"
#include "drivers/virtio-rng.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/virtio-net.hh"
#include "drivers/virtio-mmio.hh"

void arch_init_drivers()
{
    extern boot_time_chart boot_time;

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

    // Register any parsed virtio-mmio devices
    virtio::register_mmio_devices(device_manager::instance());

    // Initialize all drivers
    hw::driver_manager* drvman = hw::driver_manager::instance();
    drvman->register_driver(virtio::rng::probe);
    drvman->register_driver(virtio::blk::probe);
    drvman->register_driver(virtio::net::probe);
    boot_time.event("drivers probe");
    drvman->load_all();
    drvman->list_drivers();
}

void arch_init_early_console()
{
    console::mmio_isa_serial_console::_phys_mmio_address = 0;

    if (is_xen()) {
        new (&console::aarch64_console.xen) console::XEN_Console();
        console::arch_early_console = console::aarch64_console.xen;
        return;
    }

    int irqid;
    u64 mmio_serial_address = dtb_get_mmio_serial_console(&irqid);
    if (mmio_serial_address) {
        console::mmio_isa_serial_console::early_init(mmio_serial_address);

        new (&console::aarch64_console.isa_serial) console::mmio_isa_serial_console();
        console::aarch64_console.isa_serial.set_irqid(irqid);
        console::arch_early_console = console::aarch64_console.isa_serial;
        return;
    }

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
