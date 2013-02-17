/*-
 * Copyright (c) 1999 Michael Smith <msmith@freebsd.org>
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

#ifndef SYS_EVENTHANDLER_H
#define SYS_EVENTHANDLER_H

#include <bsd/porting/netport.h>
#include <bsd/porting/sync_stub.h>
#include <bsd/sys/sys/queue.h>

struct eventhandler_entry {
	TAILQ_ENTRY(eventhandler_entry)	ee_link;
	int				ee_priority;
	void				*ee_arg;
};

struct eventhandler_list {
	char				*el_name;
	int				el_flags;
#define EHL_INITTED	(1<<0)
	u_int				el_runcount;
	mutex_t			el_lock;
	TAILQ_ENTRY(eventhandler_list)	el_link;
	TAILQ_HEAD(,eventhandler_entry)	el_entries;
};

typedef struct eventhandler_entry	*eventhandler_tag;

#define	EHL_LOCK(p)		mutex_lock(&(p)->el_lock)
#define	EHL_UNLOCK(p)		mutex_unlock(&(p)->el_lock)
#define	EHL_LOCK_ASSERT(p, x)

/*
 * Macro to invoke the handlers for a given event.
 */
#define _EVENTHANDLER_INVOKE(name, list, ...) do {			\
	struct eventhandler_entry *_ep;					\
	struct eventhandler_entry_ ## name *_t;				\
	struct eventhandler_list *_clone;                     \
									\
	KASSERT((list)->el_flags & EHL_INITTED,				\
 	   ("eventhandler_invoke: running non-inited list"));		\
	EHL_LOCK_ASSERT((list), MA_OWNED);				\
	CTR0(KTR_EVH, "eventhandler_invoke(\"" __STRING(name) "\")");	\
	/* FIXME: What if a user deregister a callback after clone? */  \
	_clone = eventhandler_clone((list));       \
    EHL_UNLOCK((list));                     \
	TAILQ_FOREACH(_ep, &((_clone)->el_entries), ee_link) {		\
        _t = (struct eventhandler_entry_ ## name *)_ep;	\
        CTR1(KTR_EVH, "eventhandler_invoke: executing %p", \
            (void *)_t->eh_func);			\
        _t->eh_func(_ep->ee_arg , ## __VA_ARGS__);	\
	}								\
	eventhandler_free_clone(_clone); \
} while (0)

/*
 * Slow handlers are entirely dynamic; lists are created
 * when entries are added to them, and thus have no concept of "owner",
 *
 * Slow handlers need to be declared, but do not need to be defined. The
 * declaration must be in scope wherever the handler is to be invoked.
 */
#define EVENTHANDLER_DECLARE(name, type)				\
struct eventhandler_entry_ ## name 					\
{									\
	struct eventhandler_entry	ee;				\
	type				eh_func;			\
};									\
struct __hack

#define EVENTHANDLER_INVOKE(name, ...)					\
do {									\
	struct eventhandler_list *_el;					\
									\
	if ((_el = eventhandler_find_list(#name)) != NULL) 		\
		_EVENTHANDLER_INVOKE(name, _el , ## __VA_ARGS__);	\
} while (0)

#define EVENTHANDLER_REGISTER(name, func, arg, priority)		\
	eventhandler_register(NULL, #name, func, arg, priority)

#define EVENTHANDLER_DEREGISTER(name, tag) 				\
do {									\
	struct eventhandler_list *_el;					\
									\
	if ((_el = eventhandler_find_list(#name)) != NULL)		\
		eventhandler_deregister(_el, tag);			\
} while(0)
	

eventhandler_tag eventhandler_register(struct eventhandler_list *list, 
	    const char *name, void *func, void *arg, int priority);
void	eventhandler_deregister(struct eventhandler_list *list,
	    eventhandler_tag tag);
struct eventhandler_list *eventhandler_find_list(const char *name);
struct eventhandler_list *eventhandler_clone(struct eventhandler_list *list);
void eventhandler_free_clone(struct eventhandler_list *list);

/*
 * Standard system event queues.
 */

/* Generic priority levels */
#define	EVENTHANDLER_PRI_FIRST	0
#define	EVENTHANDLER_PRI_ANY	10000
#define	EVENTHANDLER_PRI_LAST	20000

/* VLAN state change events */
struct ifnet;
typedef void (*vlan_config_fn)(void *, struct ifnet *, uint16_t);
typedef void (*vlan_unconfig_fn)(void *, struct ifnet *, uint16_t);
EVENTHANDLER_DECLARE(vlan_config, vlan_config_fn);
EVENTHANDLER_DECLARE(vlan_unconfig, vlan_unconfig_fn);

/* BPF attach/detach events */
struct ifnet;
typedef void (*bpf_track_fn)(void *, struct ifnet *, int /* dlt */,
    int /* 1 =>'s attach */);
EVENTHANDLER_DECLARE(bpf_track, bpf_track_fn);

typedef void (*uma_zone_chfn)(void *);
EVENTHANDLER_DECLARE(nmbclusters_change, uma_zone_chfn);
EVENTHANDLER_DECLARE(maxsockets_change, uma_zone_chfn);


void eventhandler_init(void * __unused);

#endif /* SYS_EVENTHANDLER_H */
