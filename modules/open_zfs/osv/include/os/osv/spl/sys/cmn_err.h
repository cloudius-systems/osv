// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL cmn_err - standalone (avoids old compat chain)
 */
#ifndef _SPL_OSV_CMN_ERR_H
#define	_SPL_OSV_CMN_ERR_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Common error handling severity levels */
#define	CE_CONT		0
#define	CE_NOTE		1
#define	CE_WARN		2
#define	CE_PANIC	3
#define	CE_IGNORE	4

extern void cmn_err(int, const char *, ...);
extern void vcmn_err(int, const char *, va_list);

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_CMN_ERR_H */
