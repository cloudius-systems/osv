// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_POLICY_H
#define	_SPL_OSV_POLICY_H

/* Forward-declare xvattr_t before including compat policy.h */
struct xvattr;
typedef struct xvattr xvattr_t;

#include_next <sys/policy.h>
#endif
