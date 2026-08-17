// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_DISP_H
#define	_SPL_OSV_DISP_H

#define	KPREEMPT_SYNC	(-1)

/* kpreempt is a yield hint - on OSv it's a no-op */
#define	kpreempt(x)	do { } while (0)

#endif
