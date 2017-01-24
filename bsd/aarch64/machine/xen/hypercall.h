/*
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef __MACHINE_XEN_HYPERCALL_H__
#define __MACHINE_XEN_HYPERCALL_H__

#include <xen/interface/xen.h>

#define	ENOXENSYS	38

#ifdef __cplusplus
extern "C" {
#endif

int HYPERVISOR_sched_op(int cmd, void *arg);
int HYPERVISOR_memory_op(unsigned int cmd, void *arg);
int HYPERVISOR_multicall(multicall_entry_t *call_list, unsigned int nr_calls);
int HYPERVISOR_event_channel_op(int cmd, void *arg);
int HYPERVISOR_xen_version(int cmd, void *arg);
int HYPERVISOR_console_io(int cmd, unsigned int count, char *str);
int HYPERVISOR_physdev_op(int cmd, void *arg);
int HYPERVISOR_grant_table_op(unsigned int cmd, void *uop, unsigned int count);
int HYPERVISOR_vcpu_op(int cmd, unsigned int vcpuid, void *extra_args);
int HYPERVISOR_platform_op_raw(struct xen_platform_op *platform_op);
unsigned long HYPERVISOR_hvm_op(int op, void *arg);

#ifdef __cplusplus
} /* extern "C" */
#endif

static inline int
HYPERVISOR_platform_op(struct xen_platform_op *platform_op)
{
	platform_op->interface_version = XENPF_INTERFACE_VERSION;
	return HYPERVISOR_platform_op_raw(platform_op);
}

static inline int
HYPERVISOR_suspend(unsigned long mfn)
{
	struct sched_shutdown sched_shutdown = {
		.reason = SHUTDOWN_suspend
	};

	return HYPERVISOR_sched_op(SCHEDOP_shutdown, &sched_shutdown);
}

static inline int
HYPERVISOR_sched_op_compat(
	int cmd, unsigned long arg)
{
	abort();
}

static inline int
HYPERVISOR_update_va_mapping(
	unsigned long va, uint64_t new_val, unsigned long flags)
{
	abort();
}

static inline int
HYPERVISOR_mmu_update(
	mmu_update_t *req, unsigned int count, unsigned int *success_count,
	domid_t domid)
{
	abort();
}
#endif /* __MACHINE_XEN_HYPERCALL_H__ */
