/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014-2015 Huawei Technologies Duesseldorf GmbH
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
#include <osv/interrupt.hh>
#include <vector>

#include "gic.hh"

struct exception_frame {
    u64 regs[31];
    u64 sp;
    u64 elr;
    u64 spsr;
    u32 esr;
    u32 align1;
    u64 far;

    void *get_pc(void) { return (void *)elr; }
    unsigned int get_error(void) { return esr; }
};

extern __thread exception_frame* current_interrupt_frame;

class interrupt_desc {
public:
    interrupt_desc(interrupt_desc *old, interrupt *interrupt);
    interrupt_desc(interrupt_desc *old);

    std::vector<std::function<void ()>> handlers;
    std::vector<std::function<bool ()>> acks;
};

class interrupt_table {
public:
    interrupt_table();
    void register_interrupt(interrupt *interrupt);
    void unregister_interrupt(interrupt *interrupt);

    /* invoke_interrupt returns false if unhandled */
    bool invoke_interrupt(unsigned int id);

protected:
    void enable_irq(int id);
    void disable_irq(int id);

    unsigned int nr_irqs; /* number of supported InterruptIDs, read from gic */
    osv::rcu_ptr<interrupt_desc> irq_desc[gic::max_nr_irqs];
    mutex _lock;
};

extern class interrupt_table idt;

extern "C" {
    void page_fault(exception_frame* ef);
}

bool fixup_fault(exception_frame*);

#endif /* EXCEPTIONS_HH */
