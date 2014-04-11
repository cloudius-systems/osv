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

#include "arch-mmu.hh"

extern elf::Elf64_Ehdr* elf_header;
extern size_t elf_size;
extern void* elf_start;
extern boot_time_chart boot_time;

char *cmdline;

extern char** __argv;
extern int __argc;

void parse_cmdline(char* cmdline)
{
    char* p = cmdline;
    char* cmd = strdup(cmdline);

    static std::vector<char*> args;
    char* save;
    while ((p = strtok_r(cmd, " \t\n", &save)) != nullptr) {
        args.push_back(p);
        cmd = nullptr;
    }
    args.push_back(nullptr);
    __argv = args.data();
    __argc = args.size() - 1;
}

void setup_temporary_phys_map()
{
    // duplicate 1:1 mapping into phys_mem
    u64 *pt_ttbr0 = reinterpret_cast<u64*>(processor::read_ttbr0());
    u64 *pt_ttbr1 = reinterpret_cast<u64*>(processor::read_ttbr1());
    pt_ttbr1[mmu::pt_index(mmu::phys_mem, 3)] = pt_ttbr0[0];
    mmu::flush_tlb_all();
}

void arch_setup_free_memory()
{
    setup_temporary_phys_map();

    register u64 edata;
    asm ("adrp %0, .edata" : "=r"(edata));

    elf_start = reinterpret_cast<void*>(elf_header);
    elf_size = (u64)edata - (u64)elf_start;

    mmu::phys addr = (mmu::phys)elf_start + elf_size + 0x200000;
    addr = addr & ~0x1fffffull;

    /* set in stone for now, 512MB */
    memory::phys_mem_size = 0x20000000;
    mmu::free_initial_memory_range(addr, memory::phys_mem_size);

    /* linear_map [TTBR1] */
    mmu::linear_map(mmu::phys_mem + addr, addr, memory::phys_mem_size);

    /* linear_map [TTBR0 - ELF] */
    mmu::linear_map((void*)0x40000000, (mmu::phys)0x40000000, addr - 0x40000000);
    /* linear_map [TTBR0 - UART] */
    mmu::linear_map((void *)0x9000000, (mmu::phys)0x9000000, 0x1000);

    mmu::switch_to_runtime_page_tables();

    parse_cmdline(cmdline);
}

void arch_setup_tls(thread_control_block *tcb)
{
    asm volatile ("msr tpidr_el0, %0; isb; " :: "r"(tcb));
}

void arch_init_premain()
{
}
