// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv unistd.h supplement for OpenZFS userspace libraries.
 * Provides execvpe() stub missing from musl.
 */

#ifndef _OSV_LIBSPL_UNISTD_H
#define	_OSV_LIBSPL_UNISTD_H

#include_next <unistd.h>

/*
 * execvpe is a GNU extension not available in musl.
 * On OSv, fork/exec are not supported anyway, so this is a dead code path.
 * Provide a stub that falls back to execvp (ignoring the extra environment).
 */
#ifndef execvpe
static inline int
execvpe(const char *file, char *const argv[], char *const envp[])
{
	(void) envp;
	return (execvp(file, argv));
}
#endif

#endif /* _OSV_LIBSPL_UNISTD_H */
