#include <stdint.h>
#include <bsd/sys/amd64/include/param.h>

#ifndef UMA_STUB_H
#define UMA_STUB_H

/*
 * Header we add to the end of each item
 */
struct uma_item_header {
    uint32_t refcnt;
};

typedef struct uma_item_header* uma_item_header_t;

#define UMA_ITEM_HDR(zone, item)        ((uma_item_header_t)((void*)item+zone->uz_size))
#define UMA_ITEM_HDR_LEN                (sizeof(struct uma_item_header))


/*
 * Item constructor
 *
 * Arguments:
 *  item  A pointer to the memory which has been allocated.
 *  arg   The arg field passed to uma_zalloc_arg
 *  size  The size of the allocated item
 *  flags See zalloc flags
 *
 * Returns:
 *  0      on success
 *      errno  on failure
 *
 * Discussion:
 *  The constructor is called just before the memory is returned
 *  to the user. It may block if necessary.
 */
typedef int (*uma_ctor)(void *mem, int size, void *arg, int flags);

/*
 * Item destructor
 *
 * Arguments:
 *  item  A pointer to the memory which has been allocated.
 *  size  The size of the item being destructed.
 *  arg   Argument passed through uma_zfree_arg
 *
 * Returns:
 *  Nothing
 *
 * Discussion:
 *  The destructor may perform operations that differ from those performed
 *  by the initializer, but it must leave the object in the same state.
 *  This IS type stable storage.  This is called after EVERY zfree call.
 */
typedef void (*uma_dtor)(void *mem, int size, void *arg);

/*
 * Item initializer
 *
 * Arguments:
 *  item  A pointer to the memory which has been allocated.
 *  size  The size of the item being initialized.
 *  flags See zalloc flags
 *
 * Returns:
 *  0      on success
 *      errno  on failure
 *
 * Discussion:
 *  The initializer is called when the memory is cached in the uma zone.
 *  The initializer and the destructor should leave the object in the same
 *  state.
 */
typedef int (*uma_init)(void *mem, int size, int flags);

/*
 * Item discard function
 *
 * Arguments:
 *  item  A pointer to memory which has been 'freed' but has not left the
 *        zone's cache.
 *  size  The size of the item being discarded.
 *
 * Returns:
 *  Nothing
 *
 * Discussion:
 *  This routine is called when memory leaves a zone and is returned to the
 *  system for other uses.  It is the counter-part to the init function.
 */
typedef void (*uma_fini)(void *mem, int size);

/*
 * What's the difference between initializing and constructing?
 *
 * The item is initialized when it is cached, and this is the state that the
 * object should be in when returned to the allocator. The purpose of this is
 * to remove some code which would otherwise be called on each allocation by
 * utilizing a known, stable state.  This differs from the constructor which
 * will be called on EVERY allocation.
 *
 * For example, in the initializer you may want to initialize embedded locks,
 * NULL list pointers, set up initial states, magic numbers, etc.  This way if
 * the object is held in the allocator and re-used it won't be necessary to
 * re-initialize it.
 *
 * The constructor may be used to lock a data structure, link it on to lists,
 * bump reference counts or total counts of outstanding structures, etc.
 *
 */

/* OSv: FreeBSD uses a slab allocator called uma
 * Let's workaround this for now... */

/*
 * Zone management structure
 *
 * TODO: Optimize for cache line size
 *
 */
struct uma_zone {
    const char  *uz_name;   /* Text name of the zone */

    uma_ctor    uz_ctor;    /* Constructor for each allocation */
    uma_dtor    uz_dtor;    /* Destructor */
    uma_init    uz_init;    /* Initializer for each item */
    uma_fini    uz_fini;    /* Discards memory */

    u_int32_t   uz_flags;   /* Flags inherited from kegs */
    u_int32_t   uz_size;    /* Size inherited from kegs */

    /* zones can be nested (and called with multiple ctor?) */
    struct uma_zone* master;

};

typedef struct uma_zone * uma_zone_t;

/*
 * Backend page supplier routines
 *
 * Arguments:
 *  zone  The zone that is requesting pages.
 *  size  The number of bytes being requested.
 *  pflag Flags for these memory pages, see below.
 *  wait  Indicates our willingness to block.
 *
 * Returns:
 *  A pointer to the allocated memory or NULL on failure.
 */

typedef void *(*uma_alloc)(uma_zone_t zone, int size, u_int8_t *pflag, int wait);

/*
 * Backend page free routines
 *
 * Arguments:
 *  item  A pointer to the previously allocated pages.
 *  size  The original size of the allocation.
 *  pflag The flags for the slab.  See UMA_SLAB_* below.
 *
 * Returns:
 *  None
 */
typedef void (*uma_free)(void *item, int size, u_int8_t pflag);

void * uma_zalloc_arg(uma_zone_t zone, void *udata, int flags);
void * uma_zalloc(uma_zone_t zone, int flags);
void uma_zfree_arg(uma_zone_t zone, void *item, void *udata);
void uma_zfree(uma_zone_t zone, void *item);
void zone_drain_wait(uma_zone_t zone, int waitok);
void zone_drain(uma_zone_t zone);

/* More functions for kern_mbuf.c */


/*
 * Sets a high limit on the number of items allowed in a zone
 *
 * Arguments:
 *  zone  The zone to limit
 *  nitems  The requested upper limit on the number of items allowed
 *
 * Returns:
 *  int  The effective value of nitems after rounding up based on page size
 */
int uma_zone_set_max(uma_zone_t zone, int nitems);

/*
 * Create a new uma zone
 *
 * Arguments:
 *  name  The text name of the zone for debugging and stats. This memory
 *      should not be freed until the zone has been deallocated.
 *  size  The size of the object that is being created.
 *  ctor  The constructor that is called when the object is allocated.
 *  dtor  The destructor that is called when the object is freed.
 *  init  An initializer that sets up the initial state of the memory.
 *  fini  A discard function that undoes initialization done by init.
 *      ctor/dtor/init/fini may all be null, see notes above.
 *  align A bitmask that corresponds to the requested alignment
 *      eg 4 would be 0x3
 *  flags A set of parameters that control the behavior of the zone.
 *
 * Returns:
 *  A pointer to a structure which is intended to be opaque to users of
 *  the interface.  The value may be null if the wait flag is not set.
 */
uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor ctor,
            uma_dtor dtor, uma_init uminit, uma_fini fini,
            int align, u_int32_t flags);


/*
 * Definitions for uma_zcreate flags
 *
 * These flags share space with UMA_ZFLAGs in uma_int.h.  Be careful not to
 * overlap when adding new features.  0xf0000000 is in use by uma_int.h.
 */
#define UMA_ZONE_PAGEABLE   0x0001  /* Return items not fully backed by
                       physical memory XXX Not yet */
#define UMA_ZONE_ZINIT      0x0002  /* Initialize with zeros */
#define UMA_ZONE_STATIC     0x0004  /* Statically sized zone */
#define UMA_ZONE_OFFPAGE    0x0008  /* Force the slab structure allocation
                       off of the real memory */
#define UMA_ZONE_MALLOC     0x0010  /* For use by malloc(9) only! */
#define UMA_ZONE_NOFREE     0x0020  /* Do not free slabs of this type! */
#define UMA_ZONE_MTXCLASS   0x0040  /* Create a new lock class */
#define UMA_ZONE_VM     0x0080  /*
                     * Used for internal vm datastructures
                     * only.
                     */
#define UMA_ZONE_HASH       0x0100  /*
                     * Use a hash table instead of caching
                     * information in the vm_page.
                     */
#define UMA_ZONE_SECONDARY  0x0200  /* Zone is a Secondary Zone */
#define UMA_ZONE_REFCNT     0x0400  /* Allocate refcnts in slabs */
#define UMA_ZONE_MAXBUCKET  0x0800  /* Use largest buckets */
#define UMA_ZONE_CACHESPREAD    0x1000  /*
                     * Spread memory start locations across
                     * all possible cache lines.  May
                     * require many virtually contiguous
                     * backend pages and can fail early.
                     */
#define UMA_ZONE_VTOSLAB    0x2000  /* Zone uses vtoslab for lookup. */
#define UMA_ZONE_NODUMP     0x4000  /*
                     * Zone's pages will not be included in
                     * mini-dumps.
                     */

/*
 * These flags are shared between the keg and zone.  In zones wishing to add
 * new kegs these flags must be compatible.  Some are determined based on
 * physical parameters of the request and may not be provided by the consumer.
 */
#define UMA_ZONE_INHERIT                        \
    (UMA_ZONE_OFFPAGE | UMA_ZONE_MALLOC | UMA_ZONE_HASH |       \
    UMA_ZONE_REFCNT | UMA_ZONE_VTOSLAB)

/* Definitions for align */
#define UMA_ALIGN_PTR   (sizeof(void *) - 1)    /* Alignment fit for ptr */
#define UMA_ALIGN_LONG  (sizeof(long) - 1)  /* "" long */
#define UMA_ALIGN_INT   (sizeof(int) - 1)   /* "" int */
#define UMA_ALIGN_SHORT (sizeof(short) - 1) /* "" short */
#define UMA_ALIGN_CHAR  (sizeof(char) - 1)  /* "" char */
#define UMA_ALIGN_CACHE (0 - 1)         /* Cache line size align */


/*uma_zone_t uma_zcreate(const char *name, size_t size, uma_ctor ctor,
            uma_dtor dtor, uma_init uminit, uma_fini fini,
            int align, u_int32_t flags);
 * Create a secondary uma zone
 *
 * Arguments:
 *  name  The text name of the zone for debugging and stats. This memory
 *      should not be freed until the zone has been deallocated.
 *  ctor  The constructor that is called when the object is allocated.
 *  dtor  The destructor that is called when the object is freed.
 *  zinit  An initializer that sets up the initial state of the memory
 *      as the object passes from the Keg's slab to the Zone's cache.
 *  zfini  A discard function that undoes initialization done by init
 *      as the object passes from the Zone's cache to the Keg's slab.
 *
 *      ctor/dtor/zinit/zfini may all be null, see notes above.
 *      Note that the zinit and zfini specified here are NOT
 *      exactly the same as the init/fini specified to uma_zcreate()
 *      when creating a master zone.  These zinit/zfini are called
 *      on the TRANSITION from keg to zone (and vice-versa). Once
 *      these are set, the primary zone may alter its init/fini
 *      (which are called when the object passes from VM to keg)
 *      using uma_zone_set_init/fini()) as well as its own
 *      zinit/zfini (unset by default for master zone) with
 *      uma_zone_set_zinit/zfini() (note subtle 'z' prefix).
 *
 *  master  A reference to this zone's Master Zone (Primary Zone),
 *      which contains the backing Keg for the Secondary Zone
 *      being added.
 *
 * Returns:
 *  A pointer to a structure which is intended to be opaque to users of
 *  the interface.  The value may be null if the wait flag is not set.
 */
uma_zone_t uma_zsecond_create(char *name, uma_ctor ctor, uma_dtor dtor,
            uma_init zinit, uma_fini zfini, uma_zone_t master);

/*
 * Replaces the standard page_alloc or obj_alloc functions for this zone
 *
 * Arguments:
 *  zone   The zone whose backend allocator is being changed.
 *  allocf A pointer to the allocation function
 *
 * Returns:
 *  Nothing
 *
 * Discussion:
 *  This could be used to implement pageable allocation, or perhaps
 *  even DMA allocators if used in conjunction with the OFFPAGE
 *  zone flag.
 */

void uma_zone_set_allocf(uma_zone_t zone, uma_alloc allocf);

/*
 * Used to determine if a fixed-size zone is exhausted.
 *
 * Arguments:
 *  zone    The zone to check
 *
 * Returns:
 *  Non-zero if zone is exhausted.
 */
int uma_zone_exhausted(uma_zone_t zone);
int uma_zone_exhausted_nolock(uma_zone_t zone);


/*
 * Used to lookup the reference counter allocated for an item
 * from a UMA_ZONE_REFCNT zone.  For UMA_ZONE_REFCNT zones,
 * reference counters are allocated for items and stored in
 * the underlying slab header.
 *
 * Arguments:
 *  zone  The UMA_ZONE_REFCNT zone to which the item belongs.
 *  item  The address of the item for which we want a refcnt.
 *
 * Returns:
 *  A pointer to a u_int32_t reference counter.
 */
u_int32_t *uma_find_refcnt(uma_zone_t zone, void *item);


#endif
