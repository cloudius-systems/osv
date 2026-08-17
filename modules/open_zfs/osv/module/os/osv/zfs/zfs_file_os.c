// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * OSv ZFS file operations.
 *
 * These functions are used internally by ZFS for operations like
 * reading/writing pool configuration files (zpool.cache).
 * On OSv, most of these are stubs since we don't have a traditional
 * VFS for the config file access pattern.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_file.h>
#include <sys/stat.h>

/*
 * OSv uses a simple file structure for ZFS internal file ops.
 * These are primarily used for zpool.cache and similar config files.
 */

int
zfs_file_open(const char *path, int flags, int mode, zfs_file_t **fpp)
{
	(void) path; (void) flags; (void) mode; (void) fpp;
	/* OSv does not support zpool.cache file access yet */
	return (SET_ERROR(ENOTSUP));
}

void
zfs_file_close(zfs_file_t *fp)
{
	(void) fp;
}

int
zfs_file_write(zfs_file_t *fp, const void *buf, size_t count, ssize_t *resid)
{
	(void) fp; (void) buf; (void) count; (void) resid;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_file_pwrite(zfs_file_t *fp, const void *buf, size_t count, loff_t off,
    uint8_t ashift, ssize_t *resid)
{
	(void) fp; (void) buf; (void) count; (void) off;
	(void) ashift; (void) resid;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
	(void) fp; (void) buf; (void) count; (void) resid;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_file_pread(zfs_file_t *fp, void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	(void) fp; (void) buf; (void) count; (void) off; (void) resid;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence)
{
	(void) fp; (void) offp; (void) whence;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr)
{
	(void) fp; (void) zfattr;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_file_fsync(zfs_file_t *fp, int flags)
{
	(void) fp; (void) flags;
	return (SET_ERROR(ENOTSUP));
}

int
zfs_file_deallocate(zfs_file_t *fp, loff_t offset, loff_t len)
{
	(void) fp; (void) offset; (void) len;
	return (SET_ERROR(ENOTSUP));
}

zfs_file_t *
zfs_file_get(int fd)
{
	(void) fd;
	return (NULL);
}

void
zfs_file_put(zfs_file_t *fp)
{
	(void) fp;
}

loff_t
zfs_file_off(zfs_file_t *fp)
{
	(void) fp;
	return (0);
}

void *
zfs_file_private(zfs_file_t *fp)
{
	(void) fp;
	return (NULL);
}

int
zfs_file_unlink(const char *fnamep)
{
	(void) fnamep;
	return (SET_ERROR(ENOTSUP));
}
