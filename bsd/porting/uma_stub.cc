/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <stdint.h>
#include <assert.h>
#include <machine/param.h>
#include <bsd/porting/netport.h>
#include <bsd/porting/uma_stub.h>
#include <osv/preempt-lock.hh>
#include <osv/export.h>
#include <osv/kernel_config_memory_debug.h>
#include <osv/kernel_config_lazy_stack.h>
#include <osv/kernel_config_lazy_stack_invariant.h>

void* uma_zone::cache::alloc()
{
    if (len && !CONF_memory_debug) {
        return a[--len];
    }
    return nullptr;
}

bool uma_zone::cache::free(void* obj)
{
    if (len < max_size && !CONF_memory_debug) {
        a[len++] = obj;
        return true;
    }
    return false;
}

void * uma_zalloc_arg(uma_zone_t zone, void *udata, int flags)
{
    void * ptr;

#if CONF_lazy_stack_invariant
    assert(sched::preemptable() && arch::irq_enabled());
#endif
#if CONF_lazy_stack
    arch::ensure_next_stack_page();
#endif
    WITH_LOCK(preempt_lock) {
        ptr = (*zone->percpu_cache)->alloc();
    }

    if (!ptr) {
        auto size = zone->uz_size;
        if (zone->uz_flags & UMA_ZONE_REFCNT) {
            size += UMA_ITEM_HDR_LEN;
        }

        /*
         * Because alloc_page is faster than our malloc in the current implementation,
         * (if it ever change, we should revisit), it is worth it to take an alternate
         * path if our size + refcnt_size is exactly a page
         */
        if (size == PAGE_SIZE) {
            ptr = memory::alloc_page();
        } else {
            ptr = malloc(size);
        }

        bzero(ptr, zone->uz_size);

        // Call init
        if (zone->uz_init != NULL) {
            if (zone->uz_init(ptr, zone->uz_size, flags) != 0) {
                free(ptr);
                return (NULL);
            }
        }
    }

    // Call ctor
    if (zone->uz_ctor != NULL) {
        if (zone->uz_ctor(ptr, zone->uz_size, udata, flags) != 0) {
            free(ptr);
            return (NULL);
        }
    }

    if (flags & M_ZERO) {
        bzero(ptr, zone->uz_size);
    }

    if (zone->uz_flags & UMA_ZONE_REFCNT) {
        UMA_ITEM_HDR(zone, ptr)->refcnt = 1;
    }

    return (ptr);
}

OSV_LIBSOLARIS_API
void * uma_zalloc(uma_zone_t zone, int flags)
{
    return uma_zalloc_arg(zone, NULL, flags);
}

void uma_zfree_arg(uma_zone_t zone, void *item, void *udata)
{
    if (item == NULL) {
        return;
    }

    if (zone->uz_dtor) {
        zone->uz_dtor(item, zone->uz_size, udata);
    }

#if CONF_lazy_stack_invariant
    assert(sched::preemptable() && arch::irq_enabled());
#endif
#if CONF_lazy_stack
    arch::ensure_next_stack_page();
#endif
    WITH_LOCK(preempt_lock) {
        if ((*zone->percpu_cache)->free(item)) {
            return;
        }
    }

    if (zone->uz_fini) {
        zone->uz_fini(item, zone->uz_size);
    }

    auto effective_size = zone->uz_size;
    if (zone->uz_flags)
        effective_size += UMA_ITEM_HDR_LEN;

    if (effective_size == PAGE_SIZE) {
       memory::free_page(item);
    } else {
       free(item);
    }
}

OSV_LIBSOLARIS_API
void uma_zfree(uma_zone_t zone, void *item)
{
    uma_zfree_arg(zone, item, NULL);
}

void zone_drain_wait(uma_zone_t zone, int waitok)
{
    /* Do nothing here */
}

void zone_drain(uma_zone_t zone)
{
    zone_drain_wait(zone, M_NOWAIT);
}

int uma_zone_set_max(uma_zone_t zone, int nitems)
{
    return (nitems);
}

OSV_LIBSOLARIS_API
uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor ctor,
            uma_dtor dtor, uma_init uminit, uma_fini fini,
            int align, u_int32_t flags)
{
    uma_zone_t z = new uma_zone;

    z->uz_name = name;
    z->uz_size = size;
    z->uz_ctor = ctor;
    z->uz_dtor = dtor;
    z->uz_init = uminit;
    z->uz_fini = fini;
    z->master = NULL;
    z->uz_flags = flags;

    /* Do we need align and flags?
    args.align = align;
    args.keg = NULL;
    */

    return (z);
}

uma_zone_t uma_zsecond_create(char *name, uma_ctor ctor, uma_dtor dtor,
            uma_init zinit, uma_fini zfini, uma_zone_t master)
{
    uma_zone_t z = new uma_zone;

    z->uz_name = name;
    z->uz_size = master->uz_size;
    z->uz_ctor = ctor;
    z->uz_dtor = dtor;
    z->uz_init = zinit;
    z->uz_fini = zfini;
    z->master = master;
    z->uz_flags = master->uz_flags;

    return (z);
}


void uma_zone_set_allocf(uma_zone_t zone, uma_alloc allocf)
{
    /* Do nothing */
}

int uma_zone_exhausted(uma_zone_t zone)
{
    return 0;
}

int uma_zone_exhausted_nolock(uma_zone_t zone)
{
    return 0;
}

u_int32_t *uma_find_refcnt(uma_zone_t zone, void *item)
{
    assert(zone->uz_flags & UMA_ZONE_REFCNT);
    return &(UMA_ITEM_HDR(zone, item))->refcnt;
}

OSV_LIBSOLARIS_API
void uma_zdestroy(uma_zone_t zone)
{
    delete zone;
}
