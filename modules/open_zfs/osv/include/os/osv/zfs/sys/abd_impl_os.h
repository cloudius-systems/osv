// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv ABD implementation OS-specific header.
 * Provides abd_enter_critical/abd_exit_critical and page iteration stub.
 */
#ifndef _ABD_IMPL_OS_H
#define	_ABD_IMPL_OS_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * OSv doesn't have FreeBSD's critical_enter/exit but we need
 * preemption control stubs for the ABD RAIDZ iteration.
 */
#define	abd_enter_critical(flags)	do { } while (0)
#define	abd_exit_critical(flags)	do { } while (0)

#ifdef __cplusplus
}
#endif

#endif /* _ABD_IMPL_OS_H */
