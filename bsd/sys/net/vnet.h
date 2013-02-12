/*-
 * Copyright (c) 2006-2009 University of Zagreb
 * Copyright (c) 2006-2009 FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by the University of Zagreb and the
 * FreeBSD Foundation under sponsorship by the Stichting NLnet and the
 * FreeBSD Foundation.
 *
 * Copyright (c) 2009 Jeffrey Roberson <jeff@freebsd.org>
 * Copyright (c) 2009 Robert N. M. Watson
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*-
 * This header file defines several sets of interfaces supporting virtualized
 * network stacks:
 *
 * - Definition of 'struct vnet' and functions and macros to allocate/free/
 *   manipulate it.
 *
 * - A virtual network stack memory allocator, which provides support for
 *   virtualized global variables via a special linker set, set_vnet.
 *
 * - Virtualized sysinits/sysuninits, which allow constructors and
 *   destructors to be run for each network stack subsystem as virtual
 *   instances are created and destroyed.
 *
 * If VIMAGE isn't compiled into the kernel, virtualized global variables
 * compile to normal global variables, and virtualized sysinits to regular
 * sysinits.
 */

#ifndef _NET_VNET_H_
#define	_NET_VNET_H_

/*
 * struct vnet describes a virtualized network stack, and is primarily a
 * pointer to storage for virtualized global variables.  Expose to userspace
 * as required for libkvm.
 */
#if defined(_KERNEL) || defined(_WANT_VNET)
#include <bsd/sys/sys/queue.h>

struct vnet {
	LIST_ENTRY(vnet)	 vnet_le;	/* all vnets list */
	u_int			 vnet_magic_n;
	u_int			 vnet_ifcnt;
	u_int			 vnet_sockcnt;
	void			*vnet_data_mem;
	uintptr_t		 vnet_data_base;
};
#define	VNET_MAGIC_N	0x3e0d8f29

/*
 * These two virtual network stack allocator definitions are also required
 * for libkvm so that it can evaluate virtualized global variables.
 */
#define	VNET_SETNAME		"set_vnet"
#define	VNET_SYMPREFIX		"vnet_entry_"
#endif

#ifdef _KERNEL

/*
 * Various virtual network stack macros compile to no-ops without VIMAGE.
 */
#define	curvnet			NULL

#define	VNET_ASSERT(exp, msg)
#define	CURVNET_SET(arg)
#define	CURVNET_SET_QUIET(arg)
#define	CURVNET_RESTORE()

#define	VNET_LIST_RLOCK()
#define	VNET_LIST_RLOCK_NOSLEEP()
#define	VNET_LIST_RUNLOCK()
#define	VNET_LIST_RUNLOCK_NOSLEEP()
#define	VNET_ITERATOR_DECL(arg)
#define	VNET_FOREACH(arg)

#define	IS_DEFAULT_VNET(arg)	1
#define	CRED_TO_VNET(cr)	NULL
#define	TD_TO_VNET(td)		NULL
#define	P_TO_VNET(p)		NULL

/*
 * Versions of the VNET macros that compile to normal global variables and
 * standard sysctl definitions.
 */
#define	VNET_NAME(n)		n
#define	VNET_DECLARE(t, n)	extern t n
#define	VNET_DEFINE(t, n)	t n
#define	_VNET_PTR(b, n)		&VNET_NAME(n)

/*
 * Virtualized global variable accessor macros.
 */
#define	VNET_VNET_PTR(vnet, n)		(&(n))
#define	VNET_VNET(vnet, n)		(n)

#define	VNET_PTR(n)		(&(n))
#define	VNET(n)			(n)

/*
 * When VIMAGE isn't compiled into the kernel, virtaulized SYSCTLs simply
 * become normal SYSCTLs.
 */
#ifdef SYSCTL_OID
#define	SYSCTL_VNET_INT(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_INT(parent, nbr, name, access, ptr, val, descr)
#define	SYSCTL_VNET_PROC(parent, nbr, name, access, ptr, arg, handler,	\
	    fmt, descr)							\
	SYSCTL_PROC(parent, nbr, name, access, ptr, arg, handler, fmt,	\
	    descr)
#define	SYSCTL_VNET_OPAQUE(parent, nbr, name, access, ptr, len, fmt,    \
	    descr)							\
	SYSCTL_OPAQUE(parent, nbr, name, access, ptr, len, fmt, descr)
#define	SYSCTL_VNET_STRING(parent, nbr, name, access, arg, len, descr)	\
	SYSCTL_STRING(parent, nbr, name, access, arg, len, descr)
#define	SYSCTL_VNET_STRUCT(parent, nbr, name, access, ptr, type, descr)	\
	SYSCTL_STRUCT(parent, nbr, name, access, ptr, type, descr)
#define	SYSCTL_VNET_UINT(parent, nbr, name, access, ptr, val, descr)	\
	SYSCTL_UINT(parent, nbr, name, access, ptr, val, descr)
#define	VNET_SYSCTL_ARG(req, arg1)
#endif /* SYSCTL_OID */

/*
 * When VIMAGE isn't compiled into the kernel, VNET_SYSINIT/VNET_SYSUNINIT
 * map into normal sysinits, which have the same ordering properties.
 */
#define	VNET_SYSINIT(ident, subsystem, order, func, arg)		\
	SYSINIT(ident, subsystem, order, func, arg)
#define	VNET_SYSUNINIT(ident, subsystem, order, func, arg)		\
	SYSUNINIT(ident, subsystem, order, func, arg)

/*
 * Without VIMAGE revert to the default implementation.
 */
#define VNET_GLOBAL_EVENTHANDLER_REGISTER_TAG(tag, name, func, arg, priority) \
	(tag) = eventhandler_register(NULL, #name, func, arg, priority)
#define VNET_GLOBAL_EVENTHANDLER_REGISTER(name, func, arg, priority)	\
	eventhandler_register(NULL, #name, func, arg, priority)

#endif /* _KERNEL */

#endif /* !_NET_VNET_H_ */
