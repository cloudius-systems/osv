#include "align.hh"
#include "exceptions.hh"
#include <signal.h>
#include <stdlib.h>

namespace arch {

struct signal_frame {
    exception_frame state;
    //fpu_state fpu;
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
    rsp = align_down(rsp, 16);  // align for sse
    rsp  -= sizeof(signal_frame);
    signal_frame* frame = static_cast<signal_frame*>(rsp);
    frame->state = *ef;
    // FIXME: FPU?
    frame->si = si;
    frame->sa = sa;
    ef->rip = reinterpret_cast<ulong>(call_signal_handler_thunk);
    ef->rsp = reinterpret_cast<ulong>(rsp);
}

}

void call_signal_handler(arch::signal_frame* frame)
{
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
    // FIXME: all te other gory details
}


