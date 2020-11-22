/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/debug.h>
#include <osv/demangle.hh>

#include "dump.hh"
#include "exceptions.hh"

void dump_registers(exception_frame* ef)
{
    char name[1024];

    osv::lookup_name_demangled(
        reinterpret_cast<void *>(ef->elr), name, sizeof(name));
    debug_ll("[registers]\n");
    debug_ll("PC: 0x%016lx <%s>\n", ef->elr, name);
    debug_ll("X00: 0x%016lx X01: 0x%016lx X02: 0x%016lx\n", ef->regs[0], ef->regs[1], ef->regs[2]);
    debug_ll("X03: 0x%016lx X04: 0x%016lx X05: 0x%016lx\n", ef->regs[3], ef->regs[4], ef->regs[5]);
    debug_ll("X06: 0x%016lx X07: 0x%016lx X08: 0x%016lx\n", ef->regs[6], ef->regs[7], ef->regs[8]);
    debug_ll("X09: 0x%016lx X10: 0x%016lx X11: 0x%016lx\n", ef->regs[9], ef->regs[10], ef->regs[11]);
    debug_ll("X12: 0x%016lx X13: 0x%016lx X14: 0x%016lx\n", ef->regs[12], ef->regs[13], ef->regs[14]);
    debug_ll("X15: 0x%016lx X16: 0x%016lx X17: 0x%016lx\n", ef->regs[15], ef->regs[16], ef->regs[17]);
    debug_ll("X18: 0x%016lx X19: 0x%016lx X20: 0x%016lx\n", ef->regs[18], ef->regs[19], ef->regs[20]);
    debug_ll("X21: 0x%016lx X22: 0x%016lx X23: 0x%016lx\n", ef->regs[21], ef->regs[22], ef->regs[23]);
    debug_ll("X24: 0x%016lx X25: 0x%016lx X26: 0x%016lx\n", ef->regs[24], ef->regs[25], ef->regs[26]);
    debug_ll("X27: 0x%016lx X28: 0x%016lx X29: 0x%016lx\n", ef->regs[27], ef->regs[28], ef->regs[29]);
    debug_ll("X30: 0x%016lx SP:  0x%016lx ESR: 0x%016lx\n", ef->regs[30], ef->sp, ef->esr);
    debug_ll("PSTATE: 0x%016lx\n", ef->spsr);
}
