#include <stdio.h>
#include <string.h>
#include <mntent.h>
#include <errno.h>
#include <osv/mount.h>
#include <fs/fs.hh>
#include <sstream>
#include <libc/libc.hh>

class mtab_file final : public special_file {
    public:
        mtab_file() : special_file(FREAD, DTYPE_UNSPEC) {}

        int close() { return 0; }

        virtual int read(struct uio *uio, int flags) override;
};

int mtab_file::read(struct uio *uio, int flags)
{
    auto   fp = this;

    size_t skip = uio->uio_offset;
    if ((flags & FOF_OFFSET) == 0) {
        skip = fp->f_offset;
    }

    int    j        = 0; /* IOV index */
    int    iov_skip = 0;
    size_t bc       = 0;

    std::vector<osv::mount_desc> mounts = osv::current_mounts();

    for (size_t i = 0; i < mounts.size() && uio->uio_resid > 0; i++) {
        auto&        m = mounts[i];
        std::string  line;

        line = m.special + " " + m.path + " " + m.type + " " +
            (m.options.size() ? m.options : MNTOPT_DEFAULTS) + " 0 0\n";

        if (skip > 0) {
            if (skip >= line.size()) {
                skip -= line.size();
                continue;
            }
        }

        size_t     t  = line.size()  - skip;
        const char *q = line.c_str() + skip;

        for (; t > 0 && uio->uio_resid > 0; ) {
            struct iovec *iov = uio->uio_iov + j;
            char         *p   = static_cast<char *> (iov->iov_base) + iov_skip;
            size_t       n    = std::min(iov->iov_len - iov_skip, t);

            std::copy(q, q + n, p);

            if (n == t) {
                iov_skip += n;
            } else {
                iov_skip = 0;
                j++;
            }

            t  -= n;
            q  += n;
            bc += n;
        }
        skip = 0;
    }

    if ((flags & FOF_OFFSET) == 0) {
        fp->f_offset += bc;
    }
    uio->uio_resid -= bc;
    return 0;
}

FILE *setmntent(const char *name, const char *mode)
{
    if (!strcmp(name, "/proc/mounts") || !strcmp(name, "/etc/mnttab") || !strcmp(name, "/etc/mtab")) {
        if (strcmp("r", mode)) {
            return nullptr;
        }

        fileref f = make_file<mtab_file>();
        struct file * fp = f.get();
        if (fp == NULL) {
            return nullptr;
        }

        fhold(fp);

        int fd;
        int rc = fdalloc(fp, &fd);
        fdrop(fp);
        if (rc) {
            return nullptr;
        }

        return fdopen(fd, "r");
    }
    return fopen(name, mode);
}

int endmntent(FILE *f)
{
    fclose(f);
    return 1;
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
            fgets(linebuf, buflen, f);
            if (feof(f) || ferror(f)) return 0;
            if (!strchr(linebuf, '\n')) {
                fscanf(f, "%*[^\n]%*[\n]");
                return nullptr;
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

bool is_mtab_file(FILE *fp)
{
    fileref fr(fileref_from_fd(fileno(fp)));

    if (!fr) {
        return false;
    }

    auto mpo = dynamic_cast<mtab_file *>(fr.get());
    if (!mpo) {
        return false;
    }
    return true;
}

int addmntent(FILE *f, const struct mntent *mnt)
{
    if (is_mtab_file(f)) {
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
