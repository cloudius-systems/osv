// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL ccompile.h - compiler compatibility macros
 */

#ifndef	_SPL_SYS_CCOMPILE_H
#define	_SPL_SYS_CCOMPILE_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Linux kernel module initialization macros - no-op on OSv
 */
#ifndef __cplusplus
#define	__init
#define	__exit
#endif

/*
 * Export symbol macro - no-op on OSv
 */
#define	EXPORT_SYMBOL(x)

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_SYS_CCOMPILE_H */
