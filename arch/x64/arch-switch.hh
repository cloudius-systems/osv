#ifndef ARCH_SWITCH_HH_
#define ARCH_SWITCH_HH_

#include "msr.hh"
#include <string.h>

extern "C" {
void thread_main(void);
void thread_main_c(sched::thread* t);
}

namespace sched {

void thread::switch_to()
{
    thread* old = current();
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<u64>(_tcb));
    asm volatile
        ("push %%rbp \n\t"
         "pushq $1f \n\t"
         "mov %%rsp, (%0) \n\t"
         "mov %1, %%rsp \n\t"
         "ret \n\t"
         "1: \n\t"
         "pop %%rbp"
         :
         : "a"(&old->_state.rsp), "c"(this->_state.rsp)
         : "rbx", "rdx", "rsi", "rdi", "r8", "r9",
           "r10", "r11", "r12", "r13", "r14", "r15", "memory");
}

void thread::init_stack()
{
    void** stacktop = reinterpret_cast<void**>(_stack + sizeof(_stack));
    *--stacktop = this;
    *--stacktop = reinterpret_cast<void*>(thread_main);
    _state.rsp = stacktop;
}

void thread::setup_tcb()
{
    // FIXME: respect alignment
    void* p = malloc(sched::tls.size + sizeof(*_tcb));
    memset(p, 0, sched::tls.size);
    _tcb = static_cast<thread_control_block*>(p + tls.size);
    _tcb->self = _tcb;
    _tcb->tls_base = p;
}

void thread::setup_tcb_main()
{
    _tcb = reinterpret_cast<thread_control_block*>(processor::rdmsr(msr::IA32_FS_BASE));
}

void thread_main_c(thread* t)
{
    s_current = t;
    t->main();
    t->_waiting = true;
    schedule();
}

}

#endif /* ARCH_SWITCH_HH_ */
