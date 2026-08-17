// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv libzfs_mount_os.c
 *
 * OSv is a unikernel: there is no traditional mount table, no /proc/mounts,
 * no mount(8) utility, and no fork/exec.  ZFS mounts go through the OSv VFS
 * layer via the zfs_domount() kernel path.  Most mount OS functions are
 * stubs or minimal implementations.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/mnttab.h>
#include <sys/mntent.h>
#include <sys/dsl_crypt.h>

#include <libzfs.h>
#include <libzfs_impl.h>

/*
 * libzfs_load_module: on OSv the ZFS module is already loaded as part of
 * libsolaris.so (loaded at boot).  Nothing to do.
 */
int
libzfs_load_module(void)
{
	return (0);
}

/*
 * zfs_mount_delegation_check: OSv is a unikernel - everything runs as
 * root/privileged.  Always allow.
 */
int
zfs_mount_delegation_check(void)
{
	return (0);
}

/*
 * do_mount: perform a ZFS mount via the OSv VFS layer.
 *
 * On OSv the mount(2) syscall is available and routes into the VFS.
 * We call it directly with type "zfs".
 */
int
do_mount(zfs_handle_t *zhp, const char *mntpt, const char *opts, int flags)
{
	const char *src = zfs_get_name(zhp);
	int ret;

	/*
	 * Enable the in-memory mnttab cache on OSv.  There is no /proc/mounts
	 * so libzfs_mnttab_update() never populates the AVL tree, which
	 * causes libzfs_mnttab_add() to silently discard entries and
	 * libzfs_mnttab_find() to always return ENOENT.  By enabling the
	 * cache here, libzfs_mnttab_add() will store the entry and
	 * libzfs_mnttab_find() will locate it during zfs_unmount().
	 */
	if (!zhp->zfs_hdl->libzfs_mnttab_enable)
		libzfs_mnttab_cache(zhp->zfs_hdl, B_TRUE);

	ret = mount(src, mntpt, MNTTYPE_ZFS, flags, opts ? opts : "");
	if (ret != 0)
		return (errno);
	return (0);
}

/*
 * do_unmount: unmount a ZFS filesystem via the OSv VFS layer.
 */
int
do_unmount(zfs_handle_t *zhp, const char *mntpt, int flags)
{
	(void) zhp;
	int ret = umount2(mntpt, flags);
	return (ret < 0 ? errno : 0);
}

/*
 * zfs_adjust_mount_options: No SELinux, no special context options on OSv.
 */
void
zfs_adjust_mount_options(zfs_handle_t *zhp, const char *mntpoint,
    char *mntopts, char *mtabopt)
{
	(void) zhp, (void) mntpoint, (void) mntopts, (void) mtabopt;
}

/*
 * zfs_parse_mount_options: minimal parser - just pass options through.
 * OSv mount() takes the options string directly.
 */
int
zfs_parse_mount_options(const char *mntopts, unsigned long *mntflags,
    unsigned long *zfsflags, int sloppy, char *badopt, char *mtabopt)
{
	(void) mntopts, (void) sloppy, (void) badopt, (void) mtabopt;
	*mntflags = 0;
	*zfsflags = 0;
	return (0);
}

/*
 * zfs_mount_setattr: OSv has no mount_setattr(2).  Fall back to a full
 * remount, like FreeBSD does.
 */
int
zfs_mount_setattr(zfs_handle_t *zhp, uint32_t nspflags)
{
	(void) nspflags;
	return (zfs_mount(zhp, MNTOPT_REMOUNT, 0));
}

/* Called from the tail end of zpool_disable_datasets() */
void
zpool_disable_datasets_os(zpool_handle_t *zhp, boolean_t force)
{
	(void) zhp, (void) force;
}

/* Called from the tail end of zfs_unmount() */
void
zpool_disable_volume_os(const char *name)
{
	(void) name;
}
