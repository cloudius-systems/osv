/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "exceptions.hh"
#include <osv/debug.hh>
#include <osv/demangle.hh>
#include "dump.hh"

void dump_registers(exception_frame* ef)
{
    char name[1024];

    osv::lookup_name_demangled(
        reinterpret_cast<void *>(ef->rip), name, sizeof(name));
    debug_ll("[registers]\n");
    debug_ll("RIP: 0x%016x <%s>\n", ef->rip, name);
    debug_ll("RFL: 0x%016x  CS:  0x%016x  SS:  0x%016x\n", ef->rflags, ef->cs, ef->ss);
    debug_ll("RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx  RDX: 0x%016lx\n", ef->rax, ef->rbx, ef->rcx, ef->rdx);
    debug_ll("RSI: 0x%016lx  RDI: 0x%016lx  RBP: 0x%016lx  R8:  0x%016lx\n", ef->rsi, ef->rdi, ef->rbp, ef->r8);
    debug_ll("R9:  0x%016lx  R10: 0x%016lx  R11: 0x%016lx  R12: 0x%016lx\n", ef->r9, ef->r10, ef->r11, ef->r12);
    debug_ll("R13: 0x%016lx  R14: 0x%016lx  R15: 0x%016lx  RSP: 0x%016lx\n", ef->r9, ef->r10, ef->r11, ef->rsp);
}
