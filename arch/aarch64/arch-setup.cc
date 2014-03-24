/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch.hh"
#include "arch-setup.hh"
#include <osv/sched.hh>
#include <osv/mempool.hh>
#include <osv/elf.hh>
#include <osv/types.h>
#include <string.h>
#include <osv/boot.hh>
#include <osv/debug.hh>

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

void arch_setup_free_memory()
{
    register u64 edata;
    asm ("adrp %0, .edata" : "=r"(edata));

    elf_start = reinterpret_cast<void*>(elf_header);
    elf_size = (u64)edata - (u64)elf_start;

    /* just a test for now, we hardcode 512MB of memory */
    u64 addr = (u64)elf_start + elf_size + 0x10000;
    addr = addr & ~0xffffull;

    memory::free_initial_memory_range((void*)addr, 0x20000000);

    parse_cmdline(cmdline);
}

void arch_setup_tls(thread_control_block *tcb)
{
    asm volatile ("msr tpidr_el0, %0; isb; " :: "r"(tcb));
}

void arch_init_premain()
{
}
