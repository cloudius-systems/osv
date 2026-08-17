// SPDX-License-Identifier: CDDL-1.0
#ifndef _SPL_OSV_TIMER_H
#define	_SPL_OSV_TIMER_H

#define	ddi_time_after(a, b)	((a) > (b))
#define	ddi_time_after64(a, b)	((a) > (b))

/* usleep_range - sleep for a range of microseconds */
#include <unistd.h>
#define	usleep_range(min_us, max_us)	usleep((unsigned int)(min_us))

#endif
