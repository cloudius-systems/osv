#include <stdio.h>
#include <mntent.h>
#include <string.h>
#include <sys/mnttab.h>

#ifdef getmntent
#undef getmntent
#endif

extern "C" int bsd_getmntent(FILE *fp, struct mnttab *mp)
{
    struct mntent *e;

    e = getmntent(fp);
    if (!e) {
        return -1;
    }

    mp->mnt_special = e->mnt_fsname;
    mp->mnt_mountp  = e->mnt_dir;
    mp->mnt_fstype  = e->mnt_type;
    mp->mnt_mntopts = e->mnt_opts;

    return 0;
}

extern "C" char *bsd_hasmntopt(struct mnttab *mnt, char *opt)
{
    return strstr(mnt->mnt_mntopts, opt);
}

extern "C"
int bsd_getmntany(FILE *fp, struct mnttab *mgetp, struct mnttab *mrefp)
{
    struct mnttab mp;

    rewind(fp);

    while (!bsd_getmntent(fp, &mp)) {
        if (mrefp->mnt_special != NULL &&
                strcmp(mp.mnt_special, mrefp->mnt_special) != 0) {
            continue;
        }

        if (mrefp->mnt_mountp != NULL &&
                strcmp(mp.mnt_mountp, mrefp->mnt_mountp) != 0) {
            continue;
        }

        if (mrefp->mnt_fstype != NULL &&
                strcmp(mp.mnt_fstype, mrefp->mnt_fstype) != 0) {
            continue;
        }

        if (mrefp->mnt_mntopts != NULL &&
                strcmp(mp.mnt_mntopts, mrefp->mnt_mntopts) != 0) {
            continue;
        }

        memcpy(mgetp, &mp, sizeof(*mgetp));
        return 0;
    }

    return -1;
}
