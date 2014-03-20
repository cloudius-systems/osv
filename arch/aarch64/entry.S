/*
 * Copyright (C) 2014 Huawei Technologies Duesseldorf GmbH
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/* XXX TODO nothing to see here, move along XXX */

#include "macros.S"
        /*
        .macro	exception_entry name, handler, has_error_code
	push_pair x28, x29
	push_pair x26, x27
	push_pair x24, x25
	push_pair x22, x23
	push_pair x20, x21

	push_pair x18, x19
	push_pair x16, x17
	push_pair x14, x15
	push_pair x12, x13
	push_pair x10, x11

	push_pair x8, x9
	push_pair x6, x7
	push_pair x4, x5
	push_pair x2, x3
	push_pair x0, x1

	push_pair x29, x30
        ...
        .endm

.macro exception_error_entry name, handler
	exception_entry \name, \handler, 1
.endm

.macro exception_noerror_entry name, handler
	exception_entry \name, \handler, 0
.endm

.cfi_sections .eh_frame,  .debug_frame

.text

*/

.global thread_main
thread_main:
        .type thread_main, @function
	mov x0, x29
	bl thread_main_c

.global call_signal_handler_thunk
call_signal_handler_thunk:
        .type call_signal_handler_thunk, @function
        # stack contains a signal_frame
        /*
        .cfi_offset reg, offset
        ...
        mov x0, sp
        call call_signal_handler
        # FIXME: fpu

        pop_pair...
        add sp, sp, 16 # error_code
        */
        ret
