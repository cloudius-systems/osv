/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <xen/interface/xen.h>

#define _hypercall(call)			\
	.align 16;				\
	.globl HYPERVISOR_##call;		\
	.hidden HYPERVISOR_##call;		\
	.type HYPERVISOR_##call , "function";	\
	HYPERVISOR_##call:			\
		mov x16, #__HYPERVISOR_##call;	\
		hvc 0xEA1;			\
		ret;

.text
_hypercall(sched_op)
_hypercall(memory_op)
_hypercall(multicall)
_hypercall(event_channel_op)
_hypercall(xen_version)
_hypercall(console_io)
_hypercall(physdev_op)
_hypercall(grant_table_op)
_hypercall(vcpu_op)
_hypercall(platform_op_raw)
_hypercall(hvm_op)
