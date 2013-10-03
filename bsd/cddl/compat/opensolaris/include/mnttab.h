/* $FreeBSD$ */

#ifndef	_OPENSOLARIS_MNTTAB_H_
#define	_OPENSOLARIS_MNTTAB_H_

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/mount.h>

#include <stdio.h>
#include <paths.h>

#define	MNTTAB		_PATH_MNTTAB
#define	MNT_LINE_MAX	1024

#define	MS_OVERLAY	0x0
#define	MS_NOMNTTAB	0x0
#define	MS_RDONLY	0x1


struct mnttab {
	char	*mnt_special;
	char	*mnt_mountp;
	char	*mnt_fstype;
	char	*mnt_mntopts;
};
#define	extmnttab	mnttab

__BEGIN_DECLS

int getmntany(FILE *fd, struct mnttab *mgetp, struct mnttab *mrefp);
int getmntent(FILE *fp, struct mnttab *mp);
char *hasmntopt(struct mnttab *mnt, char *opt);

void statfs2mnttab(struct statfs *sfs, struct mnttab *mp);

__END_DECLS

#endif	/* !_OPENSOLARIS_MNTTAB_H_ */
