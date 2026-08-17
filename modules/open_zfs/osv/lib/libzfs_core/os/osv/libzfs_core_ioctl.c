// SPDX-License-Identifier: CDDL-1.0
/*
 * OSv libzfs_core_ioctl.c
 *
 * OSv ioctl dispatch for libzfs_core.  OSv provides a /dev/zfs device
 * whose ioctl handler is registered by libsolaris.so at startup.
 * Since OSv has no user/kernel address split, the ioctl passes zfs_cmd_t
 * pointers directly to the kernel ZFS subsystem.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/zfs_ioctl.h>
#include <libzfs_core.h>

int
lzc_ioctl_fd_os(int fd, unsigned long request, zfs_cmd_t *zc)
{
	return (ioctl(fd, request, zc));
}
