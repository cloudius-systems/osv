// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_VMSYSTM_H
#define	_SPL_OSV_VMSYSTM_H

/* OSv has no separate copyout/xcopyout concept */
#define	xcopyout(kaddr, uaddr, len)	((void)memcpy(uaddr, kaddr, len), 0)

#endif
