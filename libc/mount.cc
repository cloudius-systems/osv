#include "libc.hh"
#include <osv/mount.h>
#include <sys/mount.h>
#include <fs/vfs/vfs.h>

int umount(const char *path)
{
    auto r = sys_umount(path);
    if (r == 0) {
        return 0;
    } else {
        return libc_error(r);
    }
}

int umount2(const char *path, int flags)
{
    // If called with MNT_EXPIRE and either MNT_DETACH or MNT_FORCE.
    if (flags & MNT_EXPIRE && flags & (MNT_DETACH|MNT_FORCE)) {
        return libc_error(EINVAL);
    }

    auto r = sys_umount2(path, flags);
    if (r == 0) {
        return 0;
    } else {
        return libc_error(r);
    }
}

