/*
 * arch-aarch64.h
 *
 * based on arch-x86/xen.h
 *
 * Copyright (C) 2017 Sergiy Kibrik <sergiy.kibrik@globallogic.com>
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.

*/

#include "xen.h"

#ifndef __XEN_PUBLIC_ARCH_AARCH64_H__
#define __XEN_PUBLIC_ARCH_AARCH64_H__

#define ___DEFINE_XEN_GUEST_HANDLE(name, type)					\
	typedef struct { type *p; } __guest_handle_##name

#define __DEFINE_XEN_GUEST_HANDLE(name, type)			\
	___DEFINE_XEN_GUEST_HANDLE(name, type);		\
	___DEFINE_XEN_GUEST_HANDLE(const_##name, const type)

#define DEFINE_XEN_GUEST_HANDLE(name)   __DEFINE_XEN_GUEST_HANDLE(name, name)
#define XEN_GUEST_HANDLE(name)	__guest_handle_##name

#define __HYPERVISOR_platform_op_raw __HYPERVISOR_platform_op

/* Maximum number of virtual CPUs in multi-processor guests. */
#define MAX_VIRT_CPUS 1

#ifndef __ASSEMBLY__
typedef uint64_t xen_pfn_t;
#define PRI_xen_pfn "llx"
typedef uint64_t xen_ulong_t;
#define PRI_xen_ulong "llx"

struct arch_vcpu_info {
    int empty[0];	/* force zero size */
};
typedef struct arch_vcpu_info arch_vcpu_info_t;

struct arch_shared_info {
};

typedef struct arch_shared_info arch_shared_info_t;

typedef unsigned long xen_callback_t;

#endif /* __ASSEMBLY__ */

#endif /* __XEN_PUBLIC_ARCH_AARCH64_H__ */
