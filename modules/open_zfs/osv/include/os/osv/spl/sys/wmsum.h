// SPDX-License-Identifier: CDDL-1.0
/*
 * wmsum - write-mostly sum counters.
 *
 * Simple implementation using atomic operations for OSv.
 * No per-CPU optimization (OSv is typically few-CPU).
 */
#ifndef _SPL_OSV_WMSUM_H
#define	_SPL_OSV_WMSUM_H

#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
	volatile int64_t value;
} wmsum_t;

static inline void
wmsum_init(wmsum_t *ws, uint64_t value)
{
	ws->value = (int64_t)value;
}

static inline void
wmsum_fini(wmsum_t *ws)
{
	(void) ws;
}

static inline uint64_t
wmsum_value(wmsum_t *ws)
{
	return ((uint64_t)ws->value);
}

static inline void
wmsum_add(wmsum_t *ws, int64_t delta)
{
	__atomic_add_fetch(&ws->value, delta, __ATOMIC_RELAXED);
}

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_WMSUM_H */
