/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef EXCEPTIONS_HH
#define EXCEPTIONS_HH

#include <stdint.h>
#include <functional>
#include <osv/types.h>
#include <osv/rcu.hh>
#include <osv/mutex.h>
#include <vector>

#include "gic.hh"

struct exception_frame {
    u64 regs[31];
    u64 sp;
    u64 pc;
    u64 pstate;
    u32 esr;
    u32 align1;
    u64 align2; /* align to 16 */

    void *get_pc(void) { return (void *)pc; }
    unsigned int get_error(void) { return esr; }
};

extern __thread exception_frame* current_interrupt_frame;

typedef void (*interrupt_handler)(struct interrupt_desc *);

struct interrupt_desc {
    interrupt_desc(int i, interrupt_handler h, gic::irq_type t)
        : id(i), handler(h), type(t) {}

    int id;
    interrupt_handler handler;
    gic::irq_type type;
};

class interrupt_table {
public:
    interrupt_table();
    void enable_irqs();

    void register_handler(int id, interrupt_handler h, gic::irq_type t);
    int invoke_interrupt(int id); /* returns 0 if no handler registered */

protected:
    int nr_irqs; /* number of supported InterruptIDs, read from gic */
    osv::rcu_ptr<struct interrupt_desc> irq_desc[gic::max_nr_irqs];
    mutex _lock;
};

extern class interrupt_table idt;

extern "C" {
    void page_fault(exception_frame* ef);
}

bool fixup_fault(exception_frame*);

#endif /* EXCEPTIONS_HH */
