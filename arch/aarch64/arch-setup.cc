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

#include "arch-mmu.hh"
#include "arch-dtb.hh"

#include "drivers/console.hh"
#include "drivers/pl011.hh"
#include "early-console.hh"

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

    /* linear_map [TTBR0 - UART] */
    addr = (mmu::phys)console::arch_early_console.get_base_addr();
    mmu::linear_map((void *)addr, addr, 0x1000);

    /* linear_map [TTBR0 - GIC DIST and GIC CPU] */
    u64 dist, cpu;
    size_t dist_len, cpu_len;
    if (!dtb_get_gic_v2(&dist, &dist_len, &cpu, &cpu_len)) {
        dist = 0x8000000;
        dist_len = 0x10000;
        cpu = 0x8010000;
        cpu_len = 0x10000;
    }
    gic::gic = new gic::gic_driver(dist, cpu);
    mmu::linear_map((void *)dist, (mmu::phys)dist, dist_len);
    mmu::linear_map((void *)cpu, (mmu::phys)cpu, cpu_len);

    mmu::switch_to_runtime_page_tables();

    osv::parse_cmdline(cmdline);
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

void arch_init_drivers()
{
}

void arch_init_early_console()
{
    u64 addr = dtb_get_uart_base();
    if (!addr) {
        /* keep using default addresses */
        return;
    }

    console::arch_early_console.set_base_addr(addr);
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
