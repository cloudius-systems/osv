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
#include <osv/export.h>

extern "C" OSV_MODULE_API bool fsgsbase_avail;

namespace arch {

inline void set_fsbase(u64 v)
{
    barrier();
    if (fsgsbase_avail) {
        processor::wrfsbase(v);
    } else {
        processor::wrmsr(msr::IA32_FS_BASE, v);
    }
    barrier();
}

//Simple RAII utility classes that implement the logic to switch
//fsbase to the kernel address and back to the app one
class tls_switch {
    thread_control_block *_kernel_tcb;
public:
    tls_switch() {
        asm volatile ( "movq %%gs:16, %0\n\t" : "=r"(_kernel_tcb));

        //Switch to kernel tcb if app tcb present
        if (_kernel_tcb->app_tcb) {
            set_fsbase(reinterpret_cast<u64>(_kernel_tcb->self));
        }
    }

    ~tls_switch() {
        //Switch to app tcb if app tcb present
        if (_kernel_tcb->app_tcb) {
            set_fsbase(reinterpret_cast<u64>(_kernel_tcb->app_tcb));
        }
    }
};
//
//Simple RAII utility classes that implement the logic to switch
//fsbase to the specified app address and back to the kernel one
class user_tls_switch {
    thread_control_block *_kernel_tcb;
public:
    user_tls_switch() {
        asm volatile ( "movq %%gs:16, %0\n\t" : "=r"(_kernel_tcb));

        //Switch to app tcb if app tcb present
        if (_kernel_tcb->app_tcb) {
            set_fsbase(reinterpret_cast<u64>(_kernel_tcb->app_tcb));
        }
    }

    ~user_tls_switch() {
        //Switch to kernel tcb if app tcb present
        if (_kernel_tcb->app_tcb) {
            set_fsbase(reinterpret_cast<u64>(_kernel_tcb->self));
        }
    }
};

}

#endif
