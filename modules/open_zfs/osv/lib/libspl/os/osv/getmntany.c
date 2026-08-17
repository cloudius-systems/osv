// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv getmntany.c - stubs for mnttab/extmnttab queries on OSv.
 */

#include <errno.h>
#include <sys/stat.h>
#include <sys/mnttab.h>

int
getmntany(FILE *fp, struct mnttab *mgetp, struct mnttab *mrefp)
{
	(void) fp; (void) mgetp; (void) mrefp;
	return (-1);
}

int
getextmntent(const char *path, struct extmnttab *entry,
    struct stat *statbuf)
{
	(void) path; (void) entry; (void) statbuf;
	errno = ENOENT;
	return (-1);
}
