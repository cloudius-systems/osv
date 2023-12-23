/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch.hh"
#include <errno.h>
#include <osv/sched.hh>
#include "tls-switch.hh"

#define CLONE_SETTLS           0x00080000

static constexpr size_t CHILD_FRAME_OFFSET = 136;
static constexpr size_t PARENT_FRAME_OFFSET = 120;
static constexpr size_t FRAME_SIZE = 120;
static constexpr size_t RSP_OFFSET = 8;
static constexpr size_t RAX_OFFSET = 16;

sched::thread *clone_thread(unsigned long flags, void *child_stack, unsigned long newtls)
{   //
    //
    //If the parent thread is pinned we should make new thread inherit this
    auto parent_pinned_cpu = sched::thread::current()->pinned() ? sched::cpu::current() : nullptr;
    //
    //Create new child thread
    auto t = sched::thread::make([=] {
       //
       //Switch to app TCB if one specified
       u64 app_tcb = sched::thread::current()->get_app_tcb();
       if (app_tcb) {
           arch::set_fsbase(app_tcb);
       }
       //
       //Restore registers from the syscall stack and jump to the caller
       //We are restoring the registers based on how they were saved
       //on the syscall stack of the parent
       const size_t frame_offset = CHILD_FRAME_OFFSET;
       asm volatile
         ("movq %%gs:0, %%rsp \n\t"  //Switch to syscall stack
          "subq %0, %%rsp \n\t"      //Adjust stack pointer to the start of the frame
          "popq %%r15 \n\t"
          "popq %%r14 \n\t"
          "popq %%r13 \n\t"
          "popq %%r12 \n\t"
          "popq %%r11 \n\t"
          "popq %%r10 \n\t"
          "popq %%r9  \n\t"
          "popq %%r8  \n\t"
          "popq %%rdi \n\t"
          "popq %%rsi \n\t"
          "popq %%rdx \n\t"
          "popq %%rbx \n\t"
          "addq $8, %%rsp \n\t"
          "popq %%rbp \n\t"
          "popq %%rcx \n\t"
          "popq %%rax \n\t"
          "pushq %%r11 \n\t"
          "popfq \n\t"
          "popq %%rsp \n\t"          //Pop user stack to become new stack
          "jmpq *%%rcx \n\t"         //Jump to where the child thread should continue
               : : "r"(frame_offset));
    }, sched::thread::attr().
        stack(4096 * 4). //16K kernel stack should be large enough
        pin(parent_pinned_cpu),
        false,
        true);

    //
    //Copy all saved registers from parent syscall stack to the child syscall stack
    //so that they can be restored in the child thread in the inlined assembly above
    auto frame_start_on_child_syscall_stack = t->get_syscall_stack_top() - CHILD_FRAME_OFFSET;
    auto frame_start_on_parent_syscall_stack = sched::thread::current()->get_syscall_stack_top() - PARENT_FRAME_OFFSET;
    memcpy(frame_start_on_child_syscall_stack, frame_start_on_parent_syscall_stack, FRAME_SIZE);
    //
    //Save child stack pointer at the top of the frame so it will be restored last
    *reinterpret_cast<u64*>(t->get_syscall_stack_top() - RSP_OFFSET) = reinterpret_cast<u64>(child_stack);
    *reinterpret_cast<u64*>(t->get_syscall_stack_top() - RAX_OFFSET) = 0; //RAX needs to be zeroed per clone()
    //
    // Set app TCB if CLONE_SETTLS flag set
    if ((flags & CLONE_SETTLS)) {
       t->set_app_tcb(newtls);
    }

    return t;
}
