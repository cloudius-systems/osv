// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL zone.h - standalone (compat zone.h is blocked).
 * All zone-related definitions are in zfs_context_os.h.
 */
#ifndef _SPL_OSV_ZONE_H
#define	_SPL_OSV_ZONE_H

/*
 * zone_get_hostid - OSv is always global zone, no host emulation.
 */
#include <stdint.h>
static inline uint32_t
zone_get_hostid(void *ptr)
{
	(void) ptr;
	return (0);
}

#endif /* _SPL_OSV_ZONE_H */
