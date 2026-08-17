// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * Kernel module core for OSv.
 *
 * On FreeBSD this handles module load/unload, cdev creation, and
 * ioctl dispatch. On OSv, ZFS is statically linked into the kernel,
 * so we just provide the device attach/detach stubs and the
 * private state accessors that zfs_ioctl_impl.c expects.
 */

#include <sys/zfs_context.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zvol.h>
#include <sys/spa.h>

extern uint_t rrw_tsd_key;

/*
 * OSv does not have a /dev/zfs character device.
 * Ioctl operations are called directly from the VFS layer.
 */
int
zfsdev_attach(void)
{
	return (0);
}

void
zfsdev_detach(void)
{
}

/*
 * Private state accessors.
 * On FreeBSD these use devfs_set_cdevpriv / devfs_get_cdevpriv.
 * On OSv, we don't have a cdev, so these are no-ops.
 */
void
zfsdev_private_set_state(void *priv __attribute__((unused)), zfsdev_state_t *zs)
{
	(void) zs;
}

zfsdev_state_t *
zfsdev_private_get_state(void *priv)
{
	(void) priv;
	return (NULL);
}
