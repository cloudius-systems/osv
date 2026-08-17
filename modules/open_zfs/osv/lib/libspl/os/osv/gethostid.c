// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv gethostid.c - stub for libspl userspace on OSv.
 * OSv is a unikernel with no host ID concept; return 0.
 */
#include <sys/types.h>

unsigned long
get_system_hostid(void)
{
	return (0);
}
