/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#define task prex_task
#include <fs/vfs/vfs.h>
#undef task
#undef ASSERT

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/vdev_file.h>
#include <sys/vdev_impl.h>
#include <sys/zio.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>

/*
 * Virtual device vector for files.
 */

static void
vdev_file_hold(vdev_t *vd)
{
	ASSERT(vd->vdev_path != NULL);
}

static void
vdev_file_rele(vdev_t *vd)
{
	ASSERT(vd->vdev_path != NULL);
}

static int
vdev_file_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *ashift)
{
	vdev_file_t *vf;
	struct file *fp;
	int error;

	/*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (EINVAL);
	}

	/*
	 * Reopen the device if it's not currently open.  Otherwise,
	 * just update the physical size of the device.
	 */
	if (vd->vdev_tsd != NULL) {
		ASSERT(vd->vdev_reopening);
		vf = vd->vdev_tsd;
		goto skip_open;
	}

	vf = vd->vdev_tsd = kmem_zalloc(sizeof (vdev_file_t), KM_SLEEP);

	error = falloc_noinstall(&fp);
	if (error)
		goto out_free_vf;

	error = sys_open(vd->vdev_path, O_RDWR, 0, fp);
	if (error)
		goto out_fdrop;

	vf->vf_file = fp;

skip_open:
	*max_psize = *psize = vf->vf_file->f_dentry->d_vnode->v_size;
	*ashift = SPA_MINBLOCKSHIFT;

	return (0);

out_fdrop:
	fdrop(fp);
out_free_vf:
	kmem_free(vd->vdev_tsd, sizeof (vdev_file_t));
	vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
	vd->vdev_tsd = NULL;
	return error;
}

static void
vdev_file_close(vdev_t *vd)
{
	vdev_file_t *vf = vd->vdev_tsd;

	if (vd->vdev_reopening || vf == NULL)
		return;

	if (vf->vf_file != NULL)
		fdrop(vf->vf_file);

	vd->vdev_delayed_close = B_FALSE;
	kmem_free(vf, sizeof (vdev_file_t));
	vd->vdev_tsd = NULL;
}

static int
vdev_file_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_file_t *vf;
	struct file *fp;
	struct iovec iov;
	size_t count;
	int error;

	if (!vdev_readable(vd)) {
		zio->io_error = ENXIO;
		return (ZIO_PIPELINE_CONTINUE);
	}

	vf = vd->vdev_tsd;
	fp = vf->vf_file;

	switch (zio->io_type) {
	case ZIO_TYPE_READ:
	case ZIO_TYPE_WRITE:
		iov.iov_base = zio->io_data;
		iov.iov_len = zio->io_size;
		if (zio->io_type == ZIO_TYPE_READ)
			error = sys_read(fp, &iov, 1, zio->io_offset, &count);
		else
			error = sys_write(fp, &iov, 1, zio->io_offset, &count);
		if (error && count != zio->io_size)
			zio->io_error = ENOSPC;
		else
			zio->io_error = error;

		zio_interrupt(zio);
		return (ZIO_PIPELINE_STOP);
	case ZIO_TYPE_IOCTL:
#if 0
		switch (zio->io_cmd) {
		case DKIOCFLUSHWRITECACHE:
			zio->io_error = VOP_FSYNC(vp, FSYNC | FDSYNC,
			    kcred, NULL);
			break;
		default:
#endif
			zio->io_error = ENOTSUP;
//		}

		return (ZIO_PIPELINE_CONTINUE);
	default:
		abort();
	}
}

/* ARGSUSED */
static void
vdev_file_io_done(zio_t *zio)
{
}

vdev_ops_t vdev_file_ops = {
	vdev_file_open,
	vdev_file_close,
	vdev_default_asize,
	vdev_file_io_start,
	vdev_file_io_done,
	NULL,
	vdev_file_hold,
	vdev_file_rele,
	VDEV_TYPE_FILE,		/* name of this vdev type */
	B_TRUE			/* leaf vdev */
};
