/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
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
    void* rsp = reinterpret_cast<void*>(ef->rsp);
    rsp -= 128;                 // skip red zone
    rsp -= sizeof(signal_frame);
    // the Linux x86_64 calling conventions want 16-byte aligned rsp.
    // signal_frame may want even stricter alignment (but probably won't).
    rsp = align_down(rsp, std::max(16UL, alignof(signal_frame)));
    // signal_frame has no constructors/destructors, so cast is enough
    auto frame = static_cast<signal_frame*>(rsp);
    frame->state = *ef;
    frame->si = si;
    frame->sa = sa;
    ef->rip = reinterpret_cast<ulong>(call_signal_handler_thunk);
    ef->rsp = reinterpret_cast<ulong>(rsp);
}

}

unsigned __thread signal_nesting;

void call_signal_handler(arch::signal_frame* frame)
{
    if (signal_nesting) {
        // Note: nested signals are legal, but rarely used, so they usually
        // indicate trouble
        abort("nested signals");
    }
    // The user's signal handler might use the FPU, so save its current state.
    // FIXME: this fpu saving is not necessary if the callers already save the
    // FPU state. Currently, only divide_error() is missing FPU saving, and
    // callers (such as page_fault()) already save it. If we fix
    // divide_error(), we can probably get rid of the fpu saving here.
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);
    ++signal_nesting;
    if (frame->sa.sa_flags & SA_SIGINFO) {
        ucontext_t uc = {};
        auto& regs = uc.uc_mcontext.gregs;
        auto& f = frame->state;
        regs[REG_RAX] = f.rax;
        regs[REG_RBX] = f.rbx;
        regs[REG_RCX] = f.rcx;
        regs[REG_RDX] = f.rdx;
        regs[REG_RSI] = f.rsi;
        regs[REG_RDI] = f.rdi;
        regs[REG_RSP] = f.rsp;
        regs[REG_RBP] = f.rbp;
        regs[REG_R8] = f.r8;
        regs[REG_R9] = f.r9;
        regs[REG_R10] = f.r10;
        regs[REG_R11] = f.r11;
        regs[REG_R12] = f.r12;
        regs[REG_R13] = f.r13;
        regs[REG_R14] = f.r14;
        regs[REG_R15] = f.r15;
        regs[REG_RIP] = f.rip;
        frame->sa.sa_sigaction(frame->si.si_signo, &frame->si, &uc);
        f.rax = regs[REG_RAX];
        f.rbx = regs[REG_RBX];
        f.rcx = regs[REG_RCX];
        f.rdx = regs[REG_RDX];
        f.rsi = regs[REG_RSI];
        f.rdi = regs[REG_RDI];
        f.rsp = regs[REG_RSP];
        f.rbp = regs[REG_RBP];
        f.r8 = regs[REG_R8];
        f.r9 = regs[REG_R9];
        f.r10 = regs[REG_R10];
        f.r11 = regs[REG_R11];
        f.r12 = regs[REG_R12];
        f.r13 = regs[REG_R13];
        f.r14 = regs[REG_R14];
        f.r15 = regs[REG_R15];
        f.rip = regs[REG_RIP];
    } else {
        frame->sa.sa_handler(frame->si.si_signo);
    }
    --signal_nesting;
    // FIXME: all te other gory details
}


