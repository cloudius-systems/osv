/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef TLS_SWITCH_HH
#define TLS_SWITCH_HH

#include "arch.hh"
#include "arch-tls.hh"
#include <osv/barrier.hh>

namespace sched {
    extern bool fsgsbase_avail;
}

//Simple RAII utility classes that implement the logic to switch
//fsbase to the kernel address and back to the app one
namespace arch {

inline void set_fsbase(u64 v)
{
    barrier();
    if (sched::fsgsbase_avail) {
        processor::wrfsbase(v);
    } else {
        processor::wrmsr(msr::IA32_FS_BASE, v);
    }
    barrier();
}

//Intended to be used in the interrupt, page fault and other
//exceptions handlers where we know interrupts are disabled
class tls_switch_on_exception_stack {
    thread_control_block *_kernel_tcb;
public:
    tls_switch_on_exception_stack() {
        asm volatile ( "movq %%gs:16, %0\n\t" : "=r"(_kernel_tcb));

        if (_kernel_tcb->app_tcb) {
            //Switch to kernel tcb if app tcb present and on app tcb
            if (!_kernel_tcb->kernel_tcb_counter) {
                set_fsbase(reinterpret_cast<u64>(_kernel_tcb->self));
            }
            _kernel_tcb->kernel_tcb_counter++;
        }
    }

    ~tls_switch_on_exception_stack() {
        if (_kernel_tcb->app_tcb) {
            _kernel_tcb->kernel_tcb_counter--;
            if (!_kernel_tcb->kernel_tcb_counter) {
                //Restore app tcb
                set_fsbase(reinterpret_cast<u64>(_kernel_tcb->app_tcb));
            }
        }
    }
};

//Intended to be used in the syscall handler
class tls_switch_on_syscall_stack {
    thread_control_block *_kernel_tcb;
public:
    tls_switch_on_syscall_stack() {
        asm volatile ( "movq %%gs:16, %0\n\t" : "=r"(_kernel_tcb));

        if (_kernel_tcb->app_tcb) {
            //Switch to kernel tcb if app tcb present and on app tcb
            //We need to disable interrupts to make sure the switch of
            //the fs register and update of the kernel_tcb_counter
            //is atomic
            arch::irq_disable();
            if (!_kernel_tcb->kernel_tcb_counter) {
                set_fsbase(reinterpret_cast<u64>(_kernel_tcb->self));
            }
            _kernel_tcb->kernel_tcb_counter++;
            arch::irq_enable();
        }
    }

    ~tls_switch_on_syscall_stack() {
        if (_kernel_tcb->app_tcb) {
            //We need to disable interrupts to make sure the switch of
            //the fs register and update of the kernel_tcb_counter
            //is atomic
            arch::irq_disable();
            _kernel_tcb->kernel_tcb_counter--;
            if (!_kernel_tcb->kernel_tcb_counter) {
                //Restore app tcb
                set_fsbase(reinterpret_cast<u64>(_kernel_tcb->app_tcb));
            }
            arch::irq_enable();
        }
    }
};

//Intended to be used when on application thread that uses it own
//TCB and makes calls to VDSO optimized functions
class tls_switch_on_app_stack {
    thread_control_block *_kernel_tcb;
public:
    tls_switch_on_app_stack() {
        asm volatile ( "movq %%gs:16, %0\n\t" : "=r"(_kernel_tcb));

        //Switch to kernel tcb if app tcb present and on app tcb
        if (_kernel_tcb->app_tcb && !_kernel_tcb->kernel_tcb_counter) {
            //To avoid page faults on user stack when interrupts are disabled
            arch::ensure_next_stack_page();
            arch::irq_disable();
            set_fsbase(reinterpret_cast<u64>(_kernel_tcb->self));
        }
    }

    ~tls_switch_on_app_stack() {
        //Restore app tcb
        if (_kernel_tcb->app_tcb && !_kernel_tcb->kernel_tcb_counter) {
            set_fsbase(reinterpret_cast<u64>(_kernel_tcb->app_tcb));
            arch::irq_enable();
        }
    }
};

}

#endif
