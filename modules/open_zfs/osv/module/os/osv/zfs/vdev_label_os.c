// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * Vdev label OS-specific operations for OSv.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/uberblock_impl.h>
#include <sys/metaslab.h>
#include <sys/zio.h>
#include <sys/abd.h>
#include <sys/fs/zfs.h>

/*
 * Write data to vdev label pad2.
 * Used for boot environment support on FreeBSD.
 * OSv does not use this for booting, but we implement it
 * for completeness since the common code may call it.
 */
int
vdev_label_write_pad2(vdev_t *vd, const char *buf, size_t size)
{
	spa_t *spa = vd->vdev_spa;
	zio_t *zio;
	abd_t *pad2;
	int flags = ZIO_FLAG_CONFIG_WRITER | ZIO_FLAG_CANFAIL;
	int error;

	if (size > VDEV_PAD_SIZE)
		return (EINVAL);

	if (!vd->vdev_ops->vdev_op_leaf)
		return (ENODEV);
	if (vdev_is_dead(vd))
		return (ENXIO);

	pad2 = abd_alloc_for_io(VDEV_PAD_SIZE, B_TRUE);
	abd_copy_from_buf(pad2, buf, size);
	abd_zero_off(pad2, size, VDEV_PAD_SIZE - size);

retry:
	zio = zio_root(spa, NULL, NULL, flags);
	vdev_label_write(zio, vd, 0, pad2,
	    offsetof(vdev_label_t, vl_be),
	    VDEV_PAD_SIZE, NULL, NULL, flags);
	error = zio_wait(zio);
	if (error != 0 && !(flags & ZIO_FLAG_TRYHARD)) {
		flags |= ZIO_FLAG_TRYHARD;
		goto retry;
	}

	abd_free(pad2);
	return (error);
}

/*
 * Check if the reserved boot area is in-use.
 * OSv doesn't use ZFS boot blocks, so this always returns 0 (not in use).
 */
int
vdev_check_boot_reserve(spa_t *spa, vdev_t *childvd)
{
	(void) spa;
	(void) childvd;
	return (0);
}
