/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef ARCH_SWITCH_HH_
#define ARCH_SWITCH_HH_

#include "msr.hh"
#include <osv/barrier.hh>
#include <string.h>

//
// The last 16 bytes of the syscall stack are reserved for -
// tiny/large indicator and extra 8 bytes to make it 16 bytes aligned
// as Linux x64 ABI mandates.
#define SYSCALL_STACK_RESERVED_SPACE_SIZE (2 * 8)
//
// The tiny stack has to be large enough to allow for execution of
// thread::setup_large_syscall_stack() that allocates and sets up
// large syscall stack. It was measured that as of this writing
// setup_large_syscall_stack() needs a little over 600 bytes of stack
// to properly operate. This makes 1024 bytes to be an adequate size
// of tiny stack.
// All application threads pre-allocate tiny syscall stack so there
// is a tiny penalty with this solution.
#define TINY_SYSCALL_STACK_SIZE 1024
#define TINY_SYSCALL_STACK_DEPTH (TINY_SYSCALL_STACK_SIZE - SYSCALL_STACK_RESERVED_SPACE_SIZE)
//
// The large syscall stack is setup and switched to on first
// execution of SYSCALL instruction for given application thread.
#define LARGE_SYSCALL_STACK_SIZE (16 * PAGE_SIZE)
#define LARGE_SYSCALL_STACK_DEPTH (LARGE_SYSCALL_STACK_SIZE - SYSCALL_STACK_RESERVED_SPACE_SIZE)

#define SET_SYSCALL_STACK_TYPE_INDICATOR(value) \
*((long*)(_tcb->syscall_stack_top)) = value;

#define GET_SYSCALL_STACK_TYPE_INDICATOR() \
*((long*)(_tcb->syscall_stack_top))

#define TINY_SYSCALL_STACK_INDICATOR 0l
#define LARGE_SYSCALL_STACK_INDICATOR 1l

extern "C" {
void thread_main(void);
void thread_main_c(sched::thread* t);
}

namespace sched {

void set_fsbase_msr(u64 v)
{
    processor::wrmsr(msr::IA32_FS_BASE, v);
}

void set_fsbase_fsgsbase(u64 v)
{
    processor::wrfsbase(v);
}

extern "C"
void (*resolve_set_fsbase(void))(u64 v)
{
    // can't use processor::features, because it is not initialized
    // early enough.
    if (processor::features().fsgsbase) {
        return set_fsbase_fsgsbase;
    } else {
        return set_fsbase_msr;
    }
}

void set_fsbase(u64 v) __attribute__((ifunc("resolve_set_fsbase")));

void thread::switch_to()
{
    thread* old = current();
    // writing to fs_base invalidates memory accesses, so surround with
    // barriers
    barrier();
    set_fsbase(reinterpret_cast<u64>(_tcb));
    barrier();
    auto c = _detached_state->_cpu;
    old->_state.exception_stack = c->arch.get_exception_stack();
    c->arch.set_interrupt_stack(&_arch);
    c->arch.set_exception_stack(_state.exception_stack);
    auto fpucw = processor::fnstcw();
    auto mxcsr = processor::stmxcsr();
    asm volatile
        ("mov %%rbp, %c[rbp](%0) \n\t"
         "movq $1f, %c[rip](%0) \n\t"
         "mov %%rsp, %c[rsp](%0) \n\t"
         "mov %c[rsp](%1), %%rsp \n\t"
         "mov %c[rbp](%1), %%rbp \n\t"
         "jmpq *%c[rip](%1) \n\t"
         "1: \n\t"
         :
         : "a"(&old->_state), "c"(&this->_state),
           [rsp]"i"(offsetof(thread_state, rsp)),
           [rbp]"i"(offsetof(thread_state, rbp)),
           [rip]"i"(offsetof(thread_state, rip))
         : "rbx", "rdx", "rsi", "rdi", "r8", "r9",
           "r10", "r11", "r12", "r13", "r14", "r15", "memory");
    processor::fldcw(fpucw);
    processor::ldmxcsr(mxcsr);
}

void thread::switch_to_first()
{
    barrier();
    processor::wrmsr(msr::IA32_FS_BASE, reinterpret_cast<u64>(_tcb));
    barrier();
    s_current = this;
    current_cpu = _detached_state->_cpu;
    remote_thread_local_var(percpu_base) = _detached_state->_cpu->percpu_base;
    _detached_state->_cpu->arch.set_interrupt_stack(&_arch);
    _detached_state->_cpu->arch.set_exception_stack(&_arch);
    asm volatile
        ("mov %c[rsp](%0), %%rsp \n\t"
         "mov %c[rbp](%0), %%rbp \n\t"
         "jmp *%c[rip](%0)"
         :
         : "c"(&this->_state),
           [rsp]"i"(offsetof(thread_state, rsp)),
           [rbp]"i"(offsetof(thread_state, rbp)),
           [rip]"i"(offsetof(thread_state, rip))
         : "rbx", "rdx", "rsi", "rdi", "r8", "r9",
           "r10", "r11", "r12", "r13", "r14", "r15", "memory");
}

void thread::init_stack()
{
    auto& stack = _attr._stack;
    if (!stack.size) {
        stack.size = 65536;
    }
    if (!stack.begin) {
        stack.begin = malloc(stack.size);
        stack.deleter = stack.default_deleter;
    }
    void** stacktop = reinterpret_cast<void**>(stack.begin + stack.size);
    _state.rbp = this;
    _state.rip = reinterpret_cast<void*>(thread_main);
    _state.rsp = stacktop;
    _state.exception_stack = _arch.exception_stack + sizeof(_arch.exception_stack);
}

void thread::setup_tcb()
{
    assert(tls.size);

    void* user_tls_data;
    size_t user_tls_size = 0;
    if (_app_runtime) {
        auto obj = _app_runtime->app.lib();
        assert(obj);
        user_tls_size = obj->initial_tls_size();
        user_tls_data = obj->initial_tls();
    }

    // In arch/x64/loader.ld, the TLS template segment is aligned to 64
    // bytes, and that's what the objects placed in it assume. So make
    // sure our copy is allocated with the same 64-byte alignment, and
    // verify that object::init_static_tls() ensured that user_tls_size
    // also doesn't break this alignment .
    assert(align_check(tls.size, (size_t)64));
    assert(align_check(user_tls_size, (size_t)64));
    void* p = aligned_alloc(64, sched::tls.size + user_tls_size + sizeof(*_tcb));
    if (user_tls_size) {
        memcpy(p, user_tls_data, user_tls_size);
    }
    memcpy(p + user_tls_size, sched::tls.start, sched::tls.filesize);
    memset(p + user_tls_size + sched::tls.filesize, 0,
           sched::tls.size - sched::tls.filesize);
    _tcb = static_cast<thread_control_block*>(p + tls.size + user_tls_size);
    _tcb->self = _tcb;
    _tcb->tls_base = p + user_tls_size;

    if (is_app()) {
        //
        // Allocate TINY syscall call stack
        void* tiny_syscall_stack_begin = malloc(TINY_SYSCALL_STACK_SIZE);
        assert(tiny_syscall_stack_begin);
        //
        // The top of the stack needs to be 16 bytes lower to make space for
        // OSv syscall stack type indicator and extra 8 bytes to make it 16-bytes aligned
        _tcb->syscall_stack_top = tiny_syscall_stack_begin + TINY_SYSCALL_STACK_DEPTH;
        SET_SYSCALL_STACK_TYPE_INDICATOR(TINY_SYSCALL_STACK_INDICATOR);
    }
    else {
        _tcb->syscall_stack_top = 0;
    }
}

void thread::setup_large_syscall_stack()
{
    assert(is_app());
    assert(GET_SYSCALL_STACK_TYPE_INDICATOR() == TINY_SYSCALL_STACK_INDICATOR);
    //
    // Allocate LARGE syscall stack
    void* large_syscall_stack_begin = malloc(LARGE_SYSCALL_STACK_SIZE);
    void* large_syscall_stack_top = large_syscall_stack_begin + LARGE_SYSCALL_STACK_DEPTH;
    //
    // Copy all of the tiny stack to the are of last 1024 bytes of large stack.
    // This way we do not have to pop and push the same registers to be saved again.
    // Also the caller stack pointer is also copied.
    // We could have copied only last 128 (registers) + 16 bytes (2 fields) instead
    // of all of the stack but copying 1024 is simpler and happens
    // only once per thread.
    void* tiny_syscall_stack_top = _tcb->syscall_stack_top;
    memcpy(large_syscall_stack_top - TINY_SYSCALL_STACK_DEPTH,
           tiny_syscall_stack_top - TINY_SYSCALL_STACK_DEPTH, TINY_SYSCALL_STACK_SIZE);
    //
    // Save beginning of tiny stack at the bottom of LARGE stack so
    // that we can deallocate it in free_tiny_syscall_stack
    *((void**)large_syscall_stack_begin) = tiny_syscall_stack_top - TINY_SYSCALL_STACK_DEPTH;
    //
    // Set canary value (0xDEADBEAFDEADBEAF) under bottom + 8 of LARGE stack
    *((long*)(large_syscall_stack_begin + 8)) = 0xdeadbeafdeadbeaf;
    //
    // Switch syscall stack address value in TCB to the top of the LARGE one
    _tcb->syscall_stack_top = large_syscall_stack_top;
    SET_SYSCALL_STACK_TYPE_INDICATOR(LARGE_SYSCALL_STACK_INDICATOR);
}

void thread::free_tiny_syscall_stack()
{
    assert(is_app());
    assert(GET_SYSCALL_STACK_TYPE_INDICATOR() == LARGE_SYSCALL_STACK_INDICATOR);

    void* large_syscall_stack_top = _tcb->syscall_stack_top;
    void* large_syscall_stack_begin = large_syscall_stack_top - LARGE_SYSCALL_STACK_DEPTH;
    //
    // Lookup address of tiny stack saved by setup_large_syscall_stack()
    // at the bottom of LARGE stack (current syscall stack)
    void* tiny_syscall_stack_begin = *((void**)large_syscall_stack_begin);
    free(tiny_syscall_stack_begin);
}

void thread::free_tcb()
{
    if (_app_runtime) {
        auto obj = _app_runtime->app.lib();
        free(_tcb->tls_base - obj->initial_tls_size());
    } else {
        free(_tcb->tls_base);
    }

    if (_tcb->syscall_stack_top) {
        void* syscall_stack_begin = GET_SYSCALL_STACK_TYPE_INDICATOR() == TINY_SYSCALL_STACK_INDICATOR ?
            _tcb->syscall_stack_top - TINY_SYSCALL_STACK_DEPTH :
            _tcb->syscall_stack_top - LARGE_SYSCALL_STACK_DEPTH;
        free(syscall_stack_begin);
    }
}

void thread_main_c(thread* t)
{
    arch::irq_enable();
#ifdef CONF_preempt
    preempt_enable();
#endif
    // make sure thread starts with clean fpu state instead of
    // inheriting one from a previous running thread
    processor::init_fpu();
    t->main();
    t->complete();
}

extern "C" void setup_large_syscall_stack()
{
    sched::thread::current()->setup_large_syscall_stack();
}

extern "C" void free_tiny_syscall_stack()
{
    sched::thread::current()->free_tiny_syscall_stack();
}

}

#endif /* ARCH_SWITCH_HH_ */
