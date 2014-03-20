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

struct exception_frame {
    u64 regs[31];
    u64 sp;
    u64 pc;
    u64 pstate;
    u64 error_code;
    u64 reserved; /* align to 16 */
};

extern __thread exception_frame* current_interrupt_frame;

struct shared_vector {
    unsigned vector;
    unsigned id;
    shared_vector(unsigned v, unsigned i)
        : vector(v), id(i)
    {};
};

class interrupt_table {
public:
    interrupt_table();
    void load_on_cpu();
    unsigned register_handler(std::function<void ()> handler);
    // The pre_eoi should 'true' when the interrupt is for the device, 'false' otherwise.
    shared_vector register_level_triggered_handler(unsigned gsi, std::function<bool ()> pre_eoi, std::function<void ()> handler);
    void unregister_level_triggered_handler(shared_vector v);
    unsigned register_interrupt_handler(std::function<bool ()> pre_eoi, std::function<void ()> eoi, std::function<void ()> handler);
    void unregister_handler(unsigned vector);
    void invoke_interrupt(unsigned vector);
};

extern interrupt_table interrupt_table;

extern "C" {
    void page_fault(exception_frame* ef);
}

bool fixup_fault(exception_frame*);

#endif /* EXCEPTIONS_HH */
