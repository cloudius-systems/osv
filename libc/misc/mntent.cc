#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <osv/mount.h>

FILE *setmntent(const char *name, const char *mode)
{
    if (!strcmp(name, "/proc/mounts") || !strcmp(name, "/etc/mnttab") || !strcmp(name, "/etc/mtab")) {
        if (strcmp("r", mode)) {
            return nullptr;
        }

        return OSV_DYNMOUNTS;
    }
    return fopen(name, mode);
}

int endmntent(FILE *f)
{
    if (f != OSV_DYNMOUNTS) {
        fclose(f);
    }
    return 1;
}

bool osv_getmntent(char *linebuf, int buflen)
{
    // FIXME: we're using a static here in lieu of the file offset
    static size_t last = 0;
    auto mounts = osv::current_mounts();
    if (last >= mounts.size()) {
        last = 0;
        return false;
    } else {
        auto& m = mounts[last++];

        snprintf(linebuf, buflen, " %s %s %s %s 0 0",
                 m.special.c_str(),
                 m.path.c_str(),
                 m.type.c_str(),
                 m.options.size() ? m.options.c_str() : MNTOPT_DEFAULTS);
        return true;
    }
}

struct mntent *getmntent_r(FILE *f, struct mntent *mnt, char *linebuf, int buflen)
{
    int cnt, n[8];

    if (!f) {
        return nullptr;
    }

    mnt->mnt_freq = 0;
    mnt->mnt_passno = 0;

    do {

        if (f == OSV_DYNMOUNTS) {
            bool ret = osv_getmntent(linebuf, buflen);
            if (!ret) {
                return nullptr;
            }
        } else {
            fgets(linebuf, buflen, f);
            if (feof(f) || ferror(f)) return 0;
            if (!strchr(linebuf, '\n')) {
                fscanf(f, "%*[^\n]%*[\n]");
                return nullptr;
            }
        }
        cnt = sscanf(linebuf, " %n%*s%n %n%*s%n %n%*s%n %n%*s%n %d %d",
            n, n+1, n+2, n+3, n+4, n+5, n+6, n+7,
            &mnt->mnt_freq, &mnt->mnt_passno);
    } while (cnt < 2 || linebuf[n[0]] == '#');

    linebuf[n[1]] = 0;
    linebuf[n[3]] = 0;
    linebuf[n[5]] = 0;
    linebuf[n[7]] = 0;

    mnt->mnt_fsname = linebuf+n[0];
    mnt->mnt_dir = linebuf+n[2];
    mnt->mnt_type = linebuf+n[4];
    mnt->mnt_opts = linebuf+n[6];

    return mnt;
}

struct mntent *getmntent(FILE *f)
{
    static char linebuf[256];
    static struct mntent mnt;
    return getmntent_r(f, &mnt, linebuf, sizeof linebuf);
}

int addmntent(FILE *f, const struct mntent *mnt)
{
    if (f == OSV_DYNMOUNTS) {
        return 1;
    }
    if (fseek(f, 0, SEEK_END)) return 1;
    return fprintf(f, "%s\t%s\t%s\t%s\t%d\t%d\n",
        mnt->mnt_fsname, mnt->mnt_dir, mnt->mnt_type, mnt->mnt_opts,
        mnt->mnt_freq, mnt->mnt_passno) < 0;
}

char *hasmntopt(const struct mntent *mnt, const char *opt)
{
    return strstr(mnt->mnt_opts, opt);
}
