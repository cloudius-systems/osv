// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL condvar - wraps existing OSv kcondvar.h with OpenZFS extensions
 */
#ifndef _SPL_OSV_CONDVAR_H
#define	_SPL_OSV_CONDVAR_H

/* The OSv compat layer uses kcondvar.h for condvar definitions */
#include <sys/kcondvar.h>
/* Need hrtime_t and gethrtime() for cv_timedwait_hires below */
#include <sys/time.h>

/*
 * OpenZFS expects these additional condvar variants.
 * On OSv, they all map to the basic cv_timedwait or cv_wait.
 */
#ifndef cv_timedwait_io
#define	cv_timedwait_io		cv_timedwait
#endif
#ifndef cv_timedwait_idle
#define	cv_timedwait_idle	cv_timedwait
#endif
#ifndef cv_timedwait_sig
#define	cv_timedwait_sig	cv_timedwait
#endif
#ifndef cv_timedwait_sig_io
#define	cv_timedwait_sig_io	cv_timedwait
#endif
#ifndef cv_wait_io
#define	cv_wait_io		cv_wait
#endif
#ifndef cv_wait_io_sig
#define	cv_wait_io_sig		cv_wait
#endif
#ifndef cv_wait_idle
#define	cv_wait_idle		cv_wait
#endif
#ifndef cv_wait_sig
#define	cv_wait_sig		cv_wait
#endif

/*
 * High-resolution condvar wait.
 *
 * NOTE: We don't declare hz or gethrtime here because they may already
 * be defined as macros (hz) or static inline functions (gethrtime) by
 * the time this header is included. Instead, we reference them directly.
 */
static inline int
cv_timedwait_hires(kcondvar_t *cvp, mutex_t *mp, long long tim,
    long long res __attribute__((unused)), int flag)
{
	clock_t ticks;

	if (flag == 0) {
		/* Relative time: add current hrtime */
		tim += gethrtime();
	}

	/* Convert absolute hrtime (nanoseconds) to tick deadline */
	ticks = (clock_t)(tim / (1000000000LL / hz));
	return (cv_timedwait(cvp, mp, ticks));
}

#define	cv_timedwait_sig_hires		cv_timedwait_hires
#define	cv_timedwait_io_hires		cv_timedwait_hires
#define	cv_timedwait_idle_hires		cv_timedwait_hires

#endif /* _SPL_OSV_CONDVAR_H */
