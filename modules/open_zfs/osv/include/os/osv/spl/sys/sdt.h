// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL SDT (Static Defined Tracing) - no-op stubs.
 *
 * OSv doesn't have DTrace or SystemTap, so all trace points are no-ops.
 * Also provides SET_ERROR which on platforms with tracing support would
 * fire a probe.
 */
#ifndef _SPL_OSV_SDT_H
#define	_SPL_OSV_SDT_H

/* DTrace probe stubs */
#define	DTRACE_PROBE(name)
#define	DTRACE_PROBE1(name, t1, a1)
#define	DTRACE_PROBE2(name, t1, a1, t2, a2)
#define	DTRACE_PROBE3(name, t1, a1, t2, a2, t3, a3)
#define	DTRACE_PROBE4(name, t1, a1, t2, a2, t3, a3, t4, a4)
#define	DTRACE_PROBE5(name, t1, a1, t2, a2, t3, a3, t4, a4, t5, a5)

/* SDT probe stubs */
#define	SDT_PROVIDER_DECLARE(prov)
#define	SDT_PROVIDER_DEFINE(prov)
#define	SDT_PROBE_DECLARE(prov, mod, func, name)
#define	SDT_PROBE_DEFINE0(prov, mod, func, name)
#define	SDT_PROBE_DEFINE1(prov, mod, func, name, t1)
#define	SDT_PROBE_DEFINE2(prov, mod, func, name, t1, t2)
#define	SDT_PROBE_DEFINE3(prov, mod, func, name, t1, t2, t3)
#define	SDT_PROBE_DEFINE4(prov, mod, func, name, t1, t2, t3, t4)
#define	SDT_PROBE_DEFINE5(prov, mod, func, name, t1, t2, t3, t4, t5)
#define	SDT_PROBE_DEFINE6(prov, mod, func, name, t1, t2, t3, t4, t5, t6)
#define	SDT_PROBE_DEFINE7(prov, mod, func, name, t1, t2, t3, t4, t5, t6, t7)
#define	SDT_PROBE_DEFINE8(prov, mod, func, name, t1, t2, t3, t4, t5, t6, t7, t8)
#define	SDT_PROBE0(prov, mod, func, name)
#define	SDT_PROBE1(prov, mod, func, name, a1)
#define	SDT_PROBE2(prov, mod, func, name, a1, a2)
#define	SDT_PROBE3(prov, mod, func, name, a1, a2, a3)
#define	SDT_PROBE4(prov, mod, func, name, a1, a2, a3, a4)
#define	SDT_PROBE5(prov, mod, func, name, a1, a2, a3, a4, a5)
#define	SDT_PROBE6(prov, mod, func, name, a1, a2, a3, a4, a5, a6)
#define	SDT_PROBE7(prov, mod, func, name, a1, a2, a3, a4, a5, a6, a7)

/*
 * SET_ERROR - identity macro, returns the error code.
 * On platforms with tracing, this fires a DTrace probe.
 */
#define	SET_ERROR(err)	(err)

#endif /* _SPL_OSV_SDT_H */
