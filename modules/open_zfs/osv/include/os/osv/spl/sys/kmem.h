// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL kmem - standalone (avoids compat chain through netport.h)
 *
 * The compat kmem.h pulls in netport.h -> osv/uio.h -> solaris_uio.h
 * which causes UIO type conflicts with OpenZFS 2.3.6.
 */
#ifndef _SPL_OSV_KMEM_H
#define	_SPL_OSV_KMEM_H

#include <sys/types.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Memory allocation flags.
 * These must match the M_* flags from netport.h.
 */
#ifndef M_NOWAIT
#define	M_NOWAIT	0x0001
#endif
#ifndef M_WAITOK
#define	M_WAITOK	0x0002
#endif
#ifndef M_ZERO
#define	M_ZERO		0x0100
#endif
#ifndef M_NODUMP
#define	M_NODUMP	0x0800
#endif

#define	KM_SLEEP		M_WAITOK
#define	KM_PUSHPAGE		M_WAITOK
#define	KM_NOSLEEP		M_NOWAIT
#define	KM_ZERO			M_ZERO
#define	KM_NORMALPRI		0
#define	KM_NODEBUG		M_NODUMP
#define	KMC_NOTOUCH		0
#define	KMC_NODEBUG		0
#define	KMC_RECLAIMABLE		0x0
#define	KMC_KVMEM		0x0

#define	POINTER_IS_VALID(p)	(!((uintptr_t)(p) & 0x3))
#define	POINTER_INVALIDATE(pp)	(*(pp) = (void *)((uintptr_t)(*(pp)) | 0x1))

/*
 * vmem_t is just void on OSv.
 */
#define	vmem_t	void

/*
 * kmem_cache - matches the old compat definition.
 */
typedef struct kmem_cache {
	char		kc_name[32];
	size_t		kc_size;
	size_t		kc_align;
	int		(*kc_constructor)(void *, void *, int);
	void		(*kc_destructor)(void *, void *);
	void		*kc_private;
} kmem_cache_t;

/*
 * Memory allocation functions.
 */
void *zfs_kmem_alloc(size_t size, int kmflags);
void zfs_kmem_free(void *buf, size_t size);
uint64_t kmem_size(void);
uint64_t kmem_used(void);
kmem_cache_t *kmem_cache_create(char *name, size_t bufsize, size_t align,
    int (*constructor)(void *, void *, int), void (*destructor)(void *, void *),
    void (*reclaim)(void *), void *private, vmem_t *vmp, int cflags);
void kmem_cache_destroy(kmem_cache_t *cache);
void *kmem_cache_alloc(kmem_cache_t *cache, int flags);
void kmem_cache_free(kmem_cache_t *cache, void *buf);
void kmem_cache_reap_now(kmem_cache_t *cache);
void kmem_reap(void);
int kmem_debugging(void);
void *calloc(size_t n, size_t s);

#define	kmem_alloc(size, kmflags)	zfs_kmem_alloc((size), (kmflags))
#define	kmem_zalloc(size, kmflags)	zfs_kmem_alloc((size), (kmflags) | M_ZERO)
#define	kmem_free(buf, size)		zfs_kmem_free((buf), (size))

#define	kmem_cache_set_move(cache, movefunc)	do { } while (0)
#define	kmem_cache_reap_soon(cache)	kmem_cache_reap_now(cache)

/*
 * String allocation helpers.
 */
extern char *kmem_asprintf(const char *, ...)
    __attribute__((format(printf, 1, 2)));
extern char *kmem_vasprintf(const char *fmt, va_list ap)
    __attribute__((format(printf, 1, 0)));

static inline char *
kmem_strdup(const char *s)
{
	size_t len = strlen(s) + 1;
	char *buf = (char *)zfs_kmem_alloc(len, KM_SLEEP);
	memcpy(buf, s, len);
	return (buf);
}

static inline void
kmem_strfree(char *str)
{
	zfs_kmem_free(str, strlen(str) + 1);
}

extern int kmem_scnprintf(char *restrict str, size_t size,
    const char *restrict fmt, ...);

/*
 * kmem_cache stat introspection (stubs for OSv).
 */
static inline boolean_t
kmem_cache_reap_active(void)
{
	return (B_FALSE);
}

static inline uint64_t
spl_kmem_cache_inuse(kmem_cache_t *cache)
{
	(void) cache;
	return (0);
}

static inline uint64_t
spl_kmem_cache_entry_size(kmem_cache_t *cache)
{
	return (cache->kc_size);
}

/* KMALLOC_MAX_SIZE for OSv */
#ifndef KMALLOC_MAX_SIZE
#define	KMALLOC_MAX_SIZE	(4 * 1024 * 1024)
#endif

/* vmem is just kmem on OSv */
#ifndef vmem_alloc
#define	vmem_alloc(size, flags)		kmem_alloc(size, flags)
#endif
#ifndef vmem_zalloc
#define	vmem_zalloc(size, flags)	kmem_zalloc(size, flags)
#endif
#ifndef vmem_free
#define	vmem_free(ptr, size)		kmem_free(ptr, size)
#endif

/*
 * freemem and minfree - declared as actual variables because some
 * OpenZFS code (arc_os.c) uses them in extern declarations.
 * Defined in openzfs_osv_compat.c.
 */

/* kmem_cbrc_t is defined in sys/kmem_cache.h */

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_KMEM_H */
