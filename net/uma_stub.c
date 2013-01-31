#include <stdint.h>
#include <assert.h>
#include "param.h"
#include "netport.h"
#include "uma_stub.h"

/* Global zone structures */
int uma_zone_num = 0;
struct uma_zone zones[16] = {0};

/* Ref counts for shared data, save a ref-count per page.
 * a hack until we have a slab allocator */
u_int32_t uma_refcounts[1048576] = {0};


void * uma_zalloc_arg(uma_zone_t zone, void *udata, int flags)
{
    void * ptr = malloc(zone->uz_size);

    // Call init
    if (zone->uz_init != NULL) {
        if (zone->uz_init(ptr, zone->uz_size, flags) != 0) {
            return (NULL);
        }
    }

    // Call ctor
    if (zone->uz_ctor != NULL) {
        if (zone->uz_ctor(ptr, zone->uz_size, udata, flags) != 0) {
            return (NULL);
        }
    }

    if (flags & M_ZERO) {
        bzero(ptr, zone->uz_size);
    }

    return (ptr);
}

void * uma_zalloc(uma_zone_t zone, int flags)
{
    return uma_zalloc_arg(zone, NULL, flags);
}

void uma_zfree_arg(uma_zone_t zone, void *item, void *udata)
{
    if (zone->uz_dtor) {
        zone->uz_dtor(item, zone->uz_size, udata);
    }

    free(item);
}

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

uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor ctor,
            uma_dtor dtor, uma_init uminit, uma_fini fini,
            int align, u_int32_t flags)
{
    uma_zone_t z = &zones[uma_zone_num++];

    z->uz_name = name;
    z->uz_size = size;
    z->uz_ctor = ctor;
    z->uz_dtor = dtor;
    z->uz_init = uminit;
    z->uz_fini = fini;
    z->master = NULL;

    /* Do we need align and flags?
    args.align = align;
    args.flags = flags;
    args.keg = NULL;
    */

    return (z);
}

uma_zone_t uma_zsecond_create(char *name, uma_ctor ctor, uma_dtor dtor,
            uma_init zinit, uma_fini zfini, uma_zone_t master)
{
    uma_zone_t z = &zones[uma_zone_num++];

    z->uz_name = name;
    z->uz_size = master->uz_size;
    z->uz_ctor = ctor;
    z->uz_dtor = dtor;
    z->uz_init = zinit;
    z->uz_fini = zfini;
    z->master = master;

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
    /* OSv: Same ext data (Cluster) can be used by various mbuf structs, so
     * use a refcnt on the data, the ctor of Cluster set this refcnt to 1
     */

    uint64_t addr = (uint64_t)item;
    assert(addr < (1L<<32));

    unsigned slot = (uint64_t)item >> 12;
    /* Pray ref-count is smaller than 255 :) */
    return ((u_int32_t *)&uma_refcounts[slot]);
}
