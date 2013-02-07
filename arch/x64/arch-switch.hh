#ifndef ARCH_SWITCH_HH_
#define ARCH_SWITCH_HH_

#include "msr.hh"
#include "barrier.hh"
#include <string.h>

extern "C" {
void thread_main(void);
void thread_main_c(sched::thread* t);
void stack_trampoline(sched::thread* t, void (*func)(sched::thread*),
                      void** stacktop);
}

namespace sched {

void thread::switch_to()
{
    thread* old = current();
    // writing to fs_base invalidates memory accesses, so surround with
    // barriers
    barrier();
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<u64>(_tcb));
    barrier();
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

void thread::switch_to_first()
{
    barrier();
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<u64>(_tcb));
    barrier();
    s_current = this;
    asm volatile
        ("mov %0, %%rsp \n\t"
         "ret"
         :
         : "c"(this->_state.rsp)
         : "rbx", "rdx", "rsi", "rdi", "r8", "r9",
           "r10", "r11", "r12", "r13", "r14", "r15", "memory");
}

void thread::init_stack()
{
    auto& stack = _attr.stack;
    if (!stack.size) {
        stack.size = 65536;
    }
    if (!stack.begin) {
        stack.begin = malloc(stack.size);
    }
    void** stacktop = reinterpret_cast<void**>(stack.begin + stack.size);
    *--stacktop = this;
    *--stacktop = reinterpret_cast<void*>(thread_main);
    _state.rsp = stacktop;
}

void thread::on_thread_stack(thread* t)
{
    t->_func();
    t->complete();
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

void thread_main_c(thread* t)
{
    s_current = t;
    t->main();
    t->complete();
}

}

#endif /* ARCH_SWITCH_HH_ */
