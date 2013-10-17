/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "exceptions.hh"
#include "debug.hh"
#include "dump.hh"

void dump_registers(exception_frame* ef)
{
    debug_ll("registers:\n");
    debug_ll("RIP: 0x%016x  RFL: 0x%016x  CS:  0x%016x  SS:  0x%016x\n", ef->rip, ef->rflags, ef->cs, ef->ss);
    debug_ll("RAX: 0x%016x  RBX: 0x%016x  RCX: 0x%016x  RDX: 0x%016x\n", ef->rax, ef->rbx, ef->rcx, ef->rdx);
    debug_ll("RSI: 0x%016x  RDI: 0x%016x  RBP: 0x%016x  R8:  0x%016x\n", ef->rsi, ef->rdi, ef->rbp, ef->r8);
    debug_ll("R9:  0x%016x  R10: 0x%016x  R11: 0x%016x  R12: 0x%016x\n", ef->r9, ef->r10, ef->r11, ef->r12);
    debug_ll("R13: 0x%016x  R14: 0x%016x  R15: 0x%016x  RSP: 0x%016x\n", ef->r9, ef->r10, ef->r11, ef->rsp);
}
