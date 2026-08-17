// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv zone.c - stub for libspl userspace on OSv.
 * OSv has no Solaris zones; always in the global zone (0).
 */
#include <sys/types.h>

zoneid_t
getzoneid(void)
{
	return (0);
}
