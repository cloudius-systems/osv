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
#include "tls-switch.hh"

//
// The last 16 bytes of the syscall stack are reserved for -
// tiny/large indicator and extra 8 bytes to make it 16 bytes aligned
// as Linux x64 ABI mandates.
#define SYSCALL_STACK_RESERVED_SPACE_SIZE (2 * 8)
//
// The tiny stack has to be large enough to allow for execution of
// thread::setup_large_syscall_stack() that allocates and sets up
// large syscall stack and to save FPU state. It was measured that as
// of this writing setup_large_syscall_stack() needs a little over 750
// bytes of stack to properly operate. The FPU state is 850 bytes in size.
// This makes 2048 bytes to be an adequate size of tiny stack.
// In case both become larger in future, we add simple canary check
// to detect potential tiny stack overflow.
// All application threads pre-allocate tiny syscall stack so there
// is a tiny penalty with this solution.
#define TINY_SYSCALL_STACK_SIZE 2048
#define TINY_SYSCALL_STACK_DEPTH (TINY_SYSCALL_STACK_SIZE - SYSCALL_STACK_RESERVED_SPACE_SIZE)
//
// The large syscall stack is setup and switched to on first
// execution of SYSCALL instruction for given application thread.
#define LARGE_SYSCALL_STACK_SIZE (16 * PAGE_SIZE)
#define LARGE_SYSCALL_STACK_DEPTH (LARGE_SYSCALL_STACK_SIZE - SYSCALL_STACK_RESERVED_SPACE_SIZE)

#define SET_SYSCALL_STACK_TYPE_INDICATOR(value) \
*reinterpret_cast<long*>(_state._syscall_stack_descriptor.stack_top) = value;

#define GET_SYSCALL_STACK_TYPE_INDICATOR() \
*reinterpret_cast<long*>(_state._syscall_stack_descriptor.stack_top)

#define TINY_SYSCALL_STACK_INDICATOR 0l
#define LARGE_SYSCALL_STACK_INDICATOR 1l

#define STACK_CANARY 0xdeadbeafdeadbeaf

extern "C" {
void thread_main(void);
void thread_main_c(sched::thread* t);

bool fsgsbase_avail = false;
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
        fsgsbase_avail = true;
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
    // save the old thread SYSCALL caller stack pointer in the syscall stack descriptor
    old->_state._syscall_stack_descriptor.caller_stack_pointer = c->arch._current_syscall_stack_descriptor.caller_stack_pointer;
    c->arch.set_interrupt_stack(&_arch);
    c->arch.set_exception_stack(_state.exception_stack);
    // set this cpu current thread syscall stack descriptor to the values copied from the new thread syscall stack descriptor
    // so that the syscall handler can reference the current thread syscall stack top using the GS register
    c->arch._current_syscall_stack_descriptor.caller_stack_pointer = _state._syscall_stack_descriptor.caller_stack_pointer;
    c->arch._current_syscall_stack_descriptor.stack_top = _state._syscall_stack_descriptor.stack_top;
    // set this cpu current thread kernel TCB address to TCB address of the new thread
    // we are switching to
    c->arch._current_thread_kernel_tcb = reinterpret_cast<u64>(_tcb);
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
    // As the catch-all solution, reset FPU state and more specifically
    // its status word. For details why we need it please see issue #1020.
    asm volatile ("emms");
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
    _detached_state->_cpu->arch._current_thread_kernel_tcb = reinterpret_cast<u64>(_tcb);
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
    } else {
        // The thread will run thread_main_c() with preemption disabled
        // for a short while (see 695375f65303e13df1b9de798577ee9a4f8f9892)
        // so page faults are forbidden - so we need the top of the stack
        // to be pre-faulted. When we call malloc() above ourselves above
        // we know this is the case, but if the user allocates the stack
        // with mmap without MAP_STACK or MAP_POPULATE, this might not be
        // the case, so we need to fault it in now, with preemption on.
        (void) *((volatile char*)stack.begin + stack.size - 1);
    }
    void** stacktop = reinterpret_cast<void**>(stack.begin + stack.size);
    _state.rbp = this;
    _state.rip = reinterpret_cast<void*>(thread_main);
    _state.rsp = stacktop;
    _state.exception_stack = _arch.exception_stack + sizeof(_arch.exception_stack);

    if (is_app()) {
        //
        // Allocate TINY syscall call stack
        void* tiny_syscall_stack_begin = malloc(TINY_SYSCALL_STACK_SIZE);
        assert(tiny_syscall_stack_begin);
        //
        // The top of the stack needs to be 16 bytes lower to make space for
        // OSv syscall stack type indicator and extra 8 bytes to make it 16-bytes aligned
        _state._syscall_stack_descriptor.stack_top = tiny_syscall_stack_begin + TINY_SYSCALL_STACK_DEPTH;
        SET_SYSCALL_STACK_TYPE_INDICATOR(TINY_SYSCALL_STACK_INDICATOR);
        //
        // Set a canary value at the bottom of the tiny stack to catch potential overflow
        // caused by setup_large_syscall_stack()
        *reinterpret_cast<u64*>(tiny_syscall_stack_begin) = STACK_CANARY;
    }
    else {
        _state._syscall_stack_descriptor.stack_top = 0;
    }
}

void thread::setup_tcb()
{   //
    // Most importantly this method allocates TLS memory region and
    // sets up TCB (Thread Control Block) that points to that allocated
    // memory region. The TLS memory region is designated to a specific thread
    // and holds thread local variables (with __thread modifier) defined
    // in OSv kernel and the application ELF objects including dependant ones
    // through DT_NEEDED tag.
    //
    // Each ELF object and OSv kernel gets its own TLS block with offsets
    // specified in DTV structure (the offsets get calculated as ELF is loaded and symbols
    // resolved before we get to this point).
    //
    // Because both OSv kernel and position-in-dependant (pie) or position-dependant
    // executable (non library) are compiled to use local-exec mode to access the thread
    // local variables, we need to setup the offsets and TLS blocks in a special way
    // to avoid any collisions. Specifically we define OSv TLS segment
    // (see arch/x64/loader.ld for specifics) with an extra buffer at
    // the end of the kernel TLS to accommodate TLS block of pies and
    // position-dependant executables.
    //
    // Please note that the TLS layout conforms to the variant II (2),
    // which means for example that all variable offsets are negative.
    // It also means that individual objects are laid out from the right to the left.

    // (1) - TLS memory area layout with app shared library
    // |-----|-----|-----|--------------|------|
    // |SO_3 |SO_2 |SO_1 |KERNEL        |<NONE>|
    // |-----|-----|-----|--------------|------|

    // (2) - TLS memory area layout with pie or
    // position dependant executable
    //       |-----|-----|---------------------|
    //       |SO_3 |SO_2 |KERNEL        | EXE  |
    //       |-----|-----|--------------|------|

    assert(sched::tls.size);

    void* user_tls_data;
    size_t user_tls_size = 0;
    size_t executable_tls_size = 0;
    size_t aligned_executable_tls_size = 0;
    if (_app_runtime) {
        auto obj = _app_runtime->app.lib();
        assert(obj);
        user_tls_size = obj->initial_tls_size();
        user_tls_data = obj->initial_tls();
        if (obj->is_dynamically_linked_executable()) {
           executable_tls_size = obj->get_tls_size();
           aligned_executable_tls_size = obj->get_aligned_tls_size();
        }
    }

    // In arch/x64/loader.ld, the TLS template segment is aligned to 64
    // bytes, and that's what the objects placed in it assume. So make
    // sure our copy is allocated with the same 64-byte alignment, and
    // verify that object::init_static_tls() ensured that user_tls_size
    // also doesn't break this alignment.
    auto kernel_tls_size = sched::tls.size;
    assert(align_check(kernel_tls_size, (size_t)64));
    assert(align_check(user_tls_size, (size_t)64));

    auto total_tls_size = kernel_tls_size + user_tls_size;
    void* p = aligned_alloc(64, total_tls_size + sizeof(*_tcb));
    // First goes user TLS data
    if (user_tls_size) {
        memcpy(p, user_tls_data, user_tls_size);
    }
    // Next goes kernel TLS data
    auto kernel_tls_offset = user_tls_size;
    memcpy(p + kernel_tls_offset, sched::tls.start, sched::tls.filesize);
    memset(p + kernel_tls_offset + sched::tls.filesize, 0,
           kernel_tls_size - sched::tls.filesize);

    if (executable_tls_size) {
        // If executable copy its TLS block data at the designated offset
        // at the end of area as described in the ascii art for executables
        // TLS layout
        auto executable_tls_offset = total_tls_size - aligned_executable_tls_size;
        _app_runtime->app.lib()->copy_local_tls(p + executable_tls_offset);
    }
    _tcb = static_cast<thread_control_block*>(p + total_tls_size);
    _tcb->self = _tcb;
    _tcb->tls_base = p + user_tls_size;

    _tcb->app_tcb = 0;
}

void thread::setup_large_syscall_stack()
{
    // Save FPU state and restore it at the end of this function
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);

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
    void* tiny_syscall_stack_top = _state._syscall_stack_descriptor.stack_top;
    memcpy(large_syscall_stack_top - TINY_SYSCALL_STACK_DEPTH,
           tiny_syscall_stack_top - TINY_SYSCALL_STACK_DEPTH, TINY_SYSCALL_STACK_SIZE);
    //
    // Check if the tiny stack has not been overflowed
    assert(*reinterpret_cast<u64*>(_state._syscall_stack_descriptor.stack_top - TINY_SYSCALL_STACK_DEPTH) == STACK_CANARY);
    //
    // Save beginning of tiny stack at the bottom of LARGE stack so
    // that we can deallocate it in free_tiny_syscall_stack
    *((void**)large_syscall_stack_begin) = tiny_syscall_stack_top - TINY_SYSCALL_STACK_DEPTH;
    //
    // Switch syscall stack address value in TCB to the top of the LARGE one
    _state._syscall_stack_descriptor.stack_top = large_syscall_stack_top;
    SET_SYSCALL_STACK_TYPE_INDICATOR(LARGE_SYSCALL_STACK_INDICATOR);
    //
    // Switch what GS points to
     _detached_state->_cpu->arch._current_syscall_stack_descriptor.stack_top = large_syscall_stack_top;
}

void thread::free_tiny_syscall_stack()
{
    // Save FPU state and restore it at the end of this function
    sched::fpu_lock fpu;
    SCOPE_LOCK(fpu);

    assert(is_app());
    assert(GET_SYSCALL_STACK_TYPE_INDICATOR() == LARGE_SYSCALL_STACK_INDICATOR);

    void* large_syscall_stack_top = _state._syscall_stack_descriptor.stack_top;
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
}

void thread::free_syscall_stack()
{
    if (_state._syscall_stack_descriptor.stack_top) {
        void* syscall_stack_begin = GET_SYSCALL_STACK_TYPE_INDICATOR() == TINY_SYSCALL_STACK_INDICATOR ?
            _state._syscall_stack_descriptor.stack_top - TINY_SYSCALL_STACK_DEPTH :
            _state._syscall_stack_descriptor.stack_top - LARGE_SYSCALL_STACK_DEPTH;
        free(syscall_stack_begin);
    }
}

void* thread::get_syscall_stack_top()
{
    return _state._syscall_stack_descriptor.stack_top;
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
    // Switch TLS register from the app to the kernel TCB and back if necessary
    arch::tls_switch tls_switch;
    sched::thread::current()->setup_large_syscall_stack();
}

extern "C" void free_tiny_syscall_stack()
{
    // Switch TLS register from the app to the kernel TCB and back if necessary
    arch::tls_switch tls_switch;
    sched::thread::current()->free_tiny_syscall_stack();
}

}

#endif /* ARCH_SWITCH_HH_ */
