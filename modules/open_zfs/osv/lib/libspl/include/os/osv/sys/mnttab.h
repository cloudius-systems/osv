// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv sys/mnttab.h for OpenZFS userspace libraries.
 *
 * OSv has no /proc/self/mounts or /etc/mnttab.  We provide the struct
 * definitions so libzfs_dataset.c compiles, but the actual mount table
 * functions (libzfs_mnttab_update) will simply open a non-existent path
 * and return ENOENT - which is handled gracefully.
 */

#ifndef _OSV_SYS_MNTTAB_H
#define	_OSV_SYS_MNTTAB_H

#include <stdio.h>
#include <sys/types.h>

/*
 * OSv: redirect MNTTAB to a path that does not exist.
 * libzfs_mnttab_update() opens MNTTAB and bails on ENOENT - this is fine
 * because the in-memory cache is populated by libzfs_mnttab_add() instead.
 */
#ifdef MNTTAB
#undef MNTTAB
#endif
#define	MNTTAB		"/etc/mnttab"	/* openable; getmntent reads osv::current_mounts() */
#define	MNT_LINE_MAX	4108

#define	MNT_TOOLONG	1
#define	MNT_TOOMANY	2
#define	MNT_TOOFEW	3

struct mnttab {
	char	*mnt_special;
	char	*mnt_mountp;
	char	*mnt_fstype;
	char	*mnt_mntopts;
};

struct extmnttab {
	char	*mnt_special;
	char	*mnt_mountp;
	char	*mnt_fstype;
	char	*mnt_mntopts;
	unsigned int	mnt_major;
	unsigned int	mnt_minor;
};

#ifdef __cplusplus
extern "C" {
#endif

extern int getmntany(FILE *fp, struct mnttab *mp, struct mnttab *mpref);
extern int getmntent(FILE *fp, struct mnttab *mp);
extern int getextmntent(const char *path, struct extmnttab *mp,
    struct stat *statbuf);

/* hasmntopt: search for option in mnt_mntopts string */
static inline char *
hasmntopt(struct mnttab *mnt, const char *opt)
{
	char *s;
	if (mnt == NULL || mnt->mnt_mntopts == NULL || opt == NULL)
		return (NULL);
	s = mnt->mnt_mntopts;
	/* simple substring search */
	return (__extension__(__builtin_constant_p(opt)
	    ? __builtin_strstr(s, opt)
	    : (char *)__builtin_strstr(s, opt)));
}

#ifdef __cplusplus
}
#endif

#endif /* _OSV_SYS_MNTTAB_H */
