// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL time - extends the compat time.h with additional macros.
 * The compat time.h provides hrtime_t, gethrtime(), ddi_get_lbolt() etc.
 * We add the conversion macros that OpenZFS 2.3.6 needs.
 */
#ifndef _SPL_OSV_TIME_H
#define	_SPL_OSV_TIME_H

#include_next <sys/time.h>

/* These may already be defined by the compat time.h, guard them. */
#ifndef TIME_MAX
#define	TIME_MAX	LLONG_MAX
#endif

#ifndef MSEC2NSEC
#define	MSEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MILLISEC))
#endif
#ifndef NSEC2MSEC
#define	NSEC2MSEC(n)	((n) / (NANOSEC / MILLISEC))
#endif

#ifndef USEC2NSEC
#define	USEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / MICROSEC))
#endif
#ifndef NSEC2USEC
#define	NSEC2USEC(n)	((n) / (NANOSEC / MICROSEC))
#endif

#ifndef NSEC2SEC
#define	NSEC2SEC(n)	((n) / (NANOSEC / SEC))
#endif
#ifndef SEC2NSEC
#define	SEC2NSEC(m)	((hrtime_t)(m) * (NANOSEC / SEC))
#endif

#ifndef hz
extern int hz;
#endif

#ifndef SEC_TO_TICK
#define	SEC_TO_TICK(sec)	((sec) * hz)
#endif
#ifndef NSEC_TO_TICK
#define	NSEC_TO_TICK(nsec)	((nsec) / (NANOSEC / hz))
#endif
#ifndef USEC_TO_TICK
#define	USEC_TO_TICK(usec)	(howmany((hrtime_t)(usec) * hz, MICROSEC))
#endif

/*
 * getlrtime() - low-resolution (coarse) time. On OSv, gethrtime() is already
 * efficient enough that we just use it directly.
 */
#ifndef getlrtime
static inline hrtime_t
getlrtime(void)
{
	return (gethrtime());
}
#endif

#ifndef gethrtime_waitfree
#define	gethrtime_waitfree()	gethrtime()
#endif

#endif /* _SPL_OSV_TIME_H */
