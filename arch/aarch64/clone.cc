/*
 * Copyright (C) 2023 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "arch.hh"
#include <errno.h>
#include <osv/sched.hh>

#define CLONE_SETTLS           0x00080000

static constexpr size_t CHILD_FRAME_OFFSET = 7*4096 + sizeof(exception_frame);
static constexpr size_t PARENT_FRAME_OFFSET = sizeof(exception_frame);

sched::thread *clone_thread(unsigned long flags, void *child_stack, unsigned long newtls)
{   //
    //If the parent thread is pinned we should make new thread inherit this
    auto parent_pinned_cpu = sched::thread::current()->pinned() ? sched::cpu::current() : nullptr;
    //
    //Create new child thread
    auto t = sched::thread::make([=] {
       //
       //Switch to app TCB if one specified
       auto frame_start_on_exception_stack = sched::thread::current()->get_exception_stack_top() - CHILD_FRAME_OFFSET;
       exception_frame *child_frame = reinterpret_cast<exception_frame*>(frame_start_on_exception_stack);
       if (child_frame->far) {
           asm volatile ("msr tpidr_el0, %0; isb; " :: "r"(child_frame->far) : "memory");
       }
       //
       //Restore registers from the exception stack and jump to the caller
       //We are restoring the registers based on how they were saved
       //on the exception stack of the parent
       asm volatile
         ("msr daifset, #2 \n\t"          // Disable interrupts
          "isb \n\t"
          "mov sp, %0 \n\t"               // Set child stack
          "msr spsel, #0 \n\t"            // Switch to exception stack
          "mov sp, %1 \n\t"               // Set stack to the beginning of the stack frame
          "ldr x30, [sp, #256] \n\t"      // Load x30 (link register) with elr_el1 (exception link register)
          "ldp x0, x1, [sp], #16 \n\t"
          "ldp x2, x3, [sp], #16 \n\t"
          "ldp x4, x5, [sp], #16 \n\t"
          "ldp x6, x7, [sp], #16 \n\t"
          "ldp x8, x9, [sp], #16 \n\t"
          "ldp x10, x11, [sp], #16 \n\t"
          "ldp x12, x13, [sp], #16 \n\t"
          "ldp x14, x15, [sp], #16 \n\t"
          "ldp x16, x17, [sp], #16 \n\t"
          "ldp x18, x19, [sp], #16 \n\t"
          "ldp x20, x21, [sp], #16 \n\t"
          "ldp x22, x23, [sp], #16 \n\t"
          "ldp x24, x25, [sp], #16 \n\t"
          "ldp x26, x27, [sp], #16 \n\t"
          "ldp x28, x29, [sp], #16 \n\t"
          "add sp, sp, #48 \n\t"
          "add sp, sp, #28672 \n\t"       // Move back 7*4096
          "msr spsel, #1 \n\t"            // Switch to user stack
          "msr daifclr, #2 \n\t"          // Enable interrupts
          "isb \n\t" : : "r"(child_frame->sp), "r"(frame_start_on_exception_stack));
    }, sched::thread::attr().
        stack(4096 * 4). //16K kernel stack should be large enough
        pin(parent_pinned_cpu),
        false,
        true);
    //
    //Copy all saved registers from parent exception stack to the child exception stack
    //so that they can be restored in the child thread in the inlined assembly above
    auto frame_start_on_child_exception_stack = t->get_exception_stack_top() - CHILD_FRAME_OFFSET;
    exception_frame *child_frame = reinterpret_cast<exception_frame*>(frame_start_on_child_exception_stack);
    auto frame_start_on_parent_exception_stack = sched::thread::current()->get_exception_stack_top() - PARENT_FRAME_OFFSET;
    exception_frame *parent_frame = reinterpret_cast<exception_frame*>(frame_start_on_parent_exception_stack);
    memcpy(child_frame, parent_frame, sizeof(*parent_frame));
    //
    // Save child stack pointer
    child_frame->sp = reinterpret_cast<u64>(child_stack);
    child_frame->regs[0] = 0;
    //
    // Set app TCB if CLONE_SETTLS flag set
    if ((flags & CLONE_SETTLS)) {
       child_frame->far = newtls;
    } else {
       child_frame->far = 0;
    }

    return t;
}
