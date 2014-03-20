/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/align.hh>
#include "exceptions.hh"
#include <signal.h>
#include <stdlib.h>
#include <arch-cpu.hh>
#include <osv/debug.hh>

namespace arch {

struct signal_frame {
    exception_frame state;
    siginfo_t si;
    struct sigaction sa;
};

}

extern "C" {
    void call_signal_handler(arch::signal_frame* frame);
    void call_signal_handler_thunk(void);
}

namespace arch {

void build_signal_frame(exception_frame* ef,
                        const siginfo_t& si,
                        const struct sigaction& sa)
{
    void* sp = reinterpret_cast<void*>(ef->sp);
    sp -= sizeof(signal_frame);
    sp = align_down(sp, 16);
    signal_frame* frame = static_cast<signal_frame*>(sp);
    frame->state = *ef;
    frame->si = si;
    frame->sa = sa;
    ef->pc = reinterpret_cast<ulong>(call_signal_handler_thunk);
    ef->sp = reinterpret_cast<ulong>(sp);
}

}

void call_signal_handler(arch::signal_frame* frame)
{
    processor::halt_no_interrupts();
}
