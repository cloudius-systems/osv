// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv SPL string - standalone (avoids compat chain through libkern.h)
 */
#ifndef _SPL_OSV_STRING_H
#define	_SPL_OSV_STRING_H

#include <string.h>
#include <sys/kmem.h>

#ifdef __cplusplus
extern "C" {
#endif

void	strident_canon(char *, size_t);
char	*strpbrk(const char *, const char *);
extern int ddi_strtoull(const char *str, char **nptr, int base,
    u_longlong_t *result);
extern int ddi_strtoll(const char *str, char **nptr, int base,
    longlong_t *result);

#ifdef __cplusplus
}
#endif

#endif /* _SPL_OSV_STRING_H */
