// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, Cloudius Systems. All rights reserved.
 * Copyright (c) 2026, OSv contributors. All rights reserved.
 *
 * Vdev disk operations for OSv.
 *
 * This file interfaces OpenZFS vdev I/O with OSv's bio/device layer.
 * Adapted for the OpenZFS 2.3.6 vdev_ops interface which uses
 * ZIO_TYPE_FLUSH (not ZIO_TYPE_IOCTL) and void-returning io_start.
 */

#include <sys/types.h>
#include <osv/bio.h>
#include <osv/device.h>
#include <osv/prex.h>

#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/abd.h>

struct vdev_disk {
	struct device	*device;
};

static void
vdev_disk_hold(vdev_t *vd)
{
	(void) vd;
}

static void
vdev_disk_rele(vdev_t *vd)
{
	(void) vd;
}

static int
vdev_disk_open(vdev_t *vd, uint64_t *psize, uint64_t *max_psize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	struct vdev_disk *dvd;
	int error;
	char *device_name;

	/*
	 * We must have a pathname, and it must be absolute.
	 */
	if (vd->vdev_path == NULL || vd->vdev_path[0] != '/') {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	if (vd->vdev_tsd == NULL) {
		dvd = vd->vdev_tsd = kmem_zalloc(sizeof (struct vdev_disk),
		    KM_SLEEP);

		/*
		 * OSv device paths are "/dev/vblk0" etc.
		 * Strip the "/dev/" prefix for device_open().
		 */
		device_name = vd->vdev_path + 5;
		error = device_open(device_name, DO_RDWR, &dvd->device);
		if (error) {
			vd->vdev_stat.vs_aux = VDEV_AUX_OPEN_FAILED;
			kmem_free(dvd, sizeof (struct vdev_disk));
			vd->vdev_tsd = NULL;
			return (error);
		}
	} else {
		ASSERT(vd->vdev_reopening);
		dvd = vd->vdev_tsd;
	}

	*max_psize = *psize = dvd->device->size;
	/*
	 * Derive ashift from the device's logical block size.  Devices that
	 * reject sub-block I/O (e.g. Crucible's 4096-byte regions, whose
	 * write_sync/read_sync return EINVAL for unaligned offsets or lengths)
	 * would otherwise see ZFS issue 512-aligned label/uberblock writes that
	 * fail, suspending the pool during zpool_create.  device->block_size
	 * defaults to 512, so plain virtio disks still get ashift=9.
	 */
	*logical_ashift = highbit64(MAX(MAX(dvd->device->block_size, DEV_BSIZE),
	    SPA_MINBLOCKSIZE)) - 1;
	*physical_ashift = *logical_ashift;

	/*
	 * Advertise TRIM support so the ZFS trim layer will issue
	 * ZIO_TYPE_TRIM (mapped to BIO_DISCARD in vdev_disk_io_start).  OSv
	 * has no API to query a device's discard capability up front, so we
	 * optimistically enable it: devices that actually support DISCARD
	 * (virtio-blk with VIRTIO_BLK_F_DISCARD) reclaim space, and devices
	 * that do not have their BIO_DISCARD rejected with ENOTSUP, which
	 * vdev_disk_bio_done() maps so the trim layer records it as
	 * unsupported instead of faulting the vdev.  Without this,
	 * vdev_has_trim stays false and 'zpool trim' always reports
	 * 'trim operations are not supported by this device'.
	 */
	vd->vdev_has_trim = B_TRUE;
	/* Secure/discard-zeroes semantics are not guaranteed on OSv. */
	vd->vdev_has_securetrim = B_FALSE;

	return (0);
}

static void
vdev_disk_close(vdev_t *vd)
{
	struct vdev_disk *dvd = vd->vdev_tsd;

	if (vd->vdev_reopening || dvd == NULL)
		return;

	if (dvd->device)
		device_close(dvd->device);

	vd->vdev_delayed_close = B_FALSE;
	kmem_free(dvd, sizeof (struct vdev_disk));
	vd->vdev_tsd = NULL;
}

/*
 * Bio completion callback.
 *
 * For read/write bios, the ABD buffer return is handled in
 * vdev_disk_io_done (called later by the ZIO pipeline), not here.
 * We just record the error status and wake the ZIO.
 */
static void
vdev_disk_bio_done(struct bio *bio)
{
	zio_t *zio = bio->bio_caller1;

	if (bio->bio_flags & BIO_ERROR) {
		/*
		 * Preserve specific errno if set (e.g. via biofinish()).
		 * For BIO_DISCARD without a specific error, the device does
		 * not support DISCARD - report ENOTSUP so the ZFS trim layer
		 * marks trim as unsupported rather than faulting the vdev.
		 * For all other commands, fall back to EIO.
		 */
		if (bio->bio_error)
			zio->io_error = bio->bio_error;
		else if (bio->bio_cmd == BIO_DISCARD)
			zio->io_error = ENOTSUP;
		else
			zio->io_error = EIO;
	} else
		zio->io_error = 0;

	destroy_bio(bio);
	/*
	 * Do NOT clear zio->io_bio here.  It holds the borrowed ABD buffer
	 * data pointer (set in vdev_disk_io_start) which vdev_disk_io_done
	 * needs to call abd_return_buf / abd_return_buf_copy.
	 */
	zio_interrupt(zio);
}

static void
vdev_disk_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	struct vdev_disk *dvd = vd->vdev_tsd;
	struct bio *bio;

	if (dvd == NULL) {
		zio->io_error = SET_ERROR(ENXIO);
		zio_interrupt(zio);
		return;
	}

	if (zio->io_type == ZIO_TYPE_FLUSH) {
		if (!vdev_readable(vd)) {
			zio->io_error = SET_ERROR(ENXIO);
			zio_interrupt(zio);
			return;
		}

		if (zfs_nocacheflush) {
			zio_execute(zio);
			return;
		}

		if (vd->vdev_nowritecache) {
			zio->io_error = SET_ERROR(ENOTSUP);
			zio_execute(zio);
			return;
		}

		bio = alloc_bio();
		bio->bio_cmd = BIO_FLUSH;
		bio->bio_dev = dvd->device;
		bio->bio_data = NULL;
		bio->bio_offset = 0;
		bio->bio_bcount = 0;
		bio->bio_caller1 = zio;
		bio->bio_done = vdev_disk_bio_done;

		bio->bio_dev->driver->devops->strategy(bio);
		return;
	}

	if (zio->io_type == ZIO_TYPE_TRIM) {
		/*
		 * ZFS space reclamation (TRIM/DISCARD).  Issue a BIO_DISCARD
		 * for the range [io_offset, io_offset + io_size).  The
		 * virtio-blk driver translates this to a VIRTIO_BLK_T_DISCARD
		 * descriptor when VIRTIO_BLK_F_DISCARD is negotiated.  On
		 * devices that do not support DISCARD the strategy callback
		 * completes the bio with ENOTSUP, which we propagate via
		 * vdev_disk_bio_done() → zio_interrupt() → vdev_trim_cb().
		 */
		bio = alloc_bio();
		bio->bio_cmd = BIO_DISCARD;
		bio->bio_dev = dvd->device;
		bio->bio_data = NULL;
		bio->bio_offset = zio->io_offset;
		bio->bio_bcount = zio->io_size;
		bio->bio_caller1 = zio;
		bio->bio_done = vdev_disk_bio_done;

		bio->bio_dev->driver->devops->strategy(bio);
		return;
	}

	ASSERT(zio->io_type == ZIO_TYPE_READ ||
	    zio->io_type == ZIO_TYPE_WRITE);

	/*
	 * OpenZFS uses ABDs (Adaptive Buffer Descriptors).
	 * We borrow a linear buffer from the ABD for the bio.
	 */
	void *data;
	size_t size = zio->io_size;

	if (zio->io_type == ZIO_TYPE_READ) {
		data = abd_borrow_buf(zio->io_abd, size);
	} else {
		data = abd_borrow_buf_copy(zio->io_abd, size);
	}

	bio = alloc_bio();
	if (zio->io_type == ZIO_TYPE_READ)
		bio->bio_cmd = BIO_READ;
	else
		bio->bio_cmd = BIO_WRITE;

	bio->bio_dev = dvd->device;
	bio->bio_data = data;
	bio->bio_offset = zio->io_offset;
	bio->bio_bcount = size;

	bio->bio_caller1 = zio;
	bio->bio_done = vdev_disk_bio_done;

	/*
	 * Store the bio pointer in zio->io_bio so vdev_disk_io_done
	 * can find the borrowed buffer data pointer for ABD return.
	 * We store the data pointer in io_bio since the bio itself
	 * will be destroyed in the completion callback.
	 */
	zio->io_bio = data;

	bio->bio_dev->driver->devops->strategy(bio);
}

static void
vdev_disk_io_done(zio_t *zio)
{
	if (zio->io_type != ZIO_TYPE_READ && zio->io_type != ZIO_TYPE_WRITE)
		return;

	if (zio->io_bio == NULL)
		return;

	/*
	 * Return the ABD borrowed buffer. io_bio holds the data pointer
	 * that was borrowed before the bio was submitted.
	 */
	void *data = zio->io_bio;
	zio->io_bio = NULL;

	if (zio->io_type == ZIO_TYPE_READ) {
		abd_return_buf_copy(zio->io_abd, data, zio->io_size);
	} else {
		abd_return_buf(zio->io_abd, data, zio->io_size);
	}
}

vdev_ops_t vdev_disk_ops = {
	.vdev_op_init = NULL,
	.vdev_op_fini = NULL,
	.vdev_op_open = vdev_disk_open,
	.vdev_op_close = vdev_disk_close,
	.vdev_op_psize_to_asize = vdev_default_asize,
	.vdev_op_asize_to_psize = vdev_default_psize,
	.vdev_op_min_asize = vdev_default_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_disk_io_start,
	.vdev_op_io_done = vdev_disk_io_done,
	.vdev_op_state_change = NULL,
	.vdev_op_need_resilver = NULL,
	.vdev_op_hold = vdev_disk_hold,
	.vdev_op_rele = vdev_disk_rele,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_default_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = NULL,
	.vdev_op_nparity = NULL,
	.vdev_op_ndisks = NULL,
	.vdev_op_kobj_evt_post = NULL,
	.vdev_op_type = VDEV_TYPE_DISK,
	.vdev_op_leaf = B_TRUE,
};

/*
 * Synchronous physical I/O helper.
 */
static int
vdev_disk_physio(struct device *dev, caddr_t data, size_t size,
    uint64_t offset, int write)
{
	struct bio *bio;
	int ret;

	bio = alloc_bio();
	if (write)
		bio->bio_cmd = BIO_WRITE;
	else
		bio->bio_cmd = BIO_READ;

	bio->bio_dev = dev;
	bio->bio_data = data;
	bio->bio_offset = offset;
	bio->bio_bcount = size;

	bio->bio_dev->driver->devops->strategy(bio);

	ret = bio_wait(bio);
	destroy_bio(bio);

	return (ret);
}

/*
 * Given a disk device path, read the ZFS label and construct
 * a configuration nvlist.
 */
int
vdev_disk_read_rootlabel(char *devpath, nvlist_t **config)
{
	vdev_label_t *label;
	struct device *dev;
	uint64_t size;
	int l;
	int error = -1;

	error = device_open(devpath + 5, DO_RDWR, &dev);
	if (error)
		return (error);

	size = P2ALIGN_TYPED(dev->size, sizeof (vdev_label_t), uint64_t);
	label = kmem_alloc(sizeof (vdev_label_t), KM_SLEEP);

	*config = NULL;
	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t offset, state, txg = 0;

		offset = vdev_label_offset(size, l, 0);
		if (vdev_disk_physio(dev, (caddr_t)label,
		    VDEV_SKIP_SIZE + VDEV_PHYS_SIZE, offset, 0) != 0)
			continue;

		if (nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
		    sizeof (label->vl_vdev_phys.vp_nvlist), config, 0) != 0) {
			*config = NULL;
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_STATE,
		    &state) != 0 || state >= POOL_STATE_DESTROYED) {
			nvlist_free(*config);
			*config = NULL;
			continue;
		}

		if (nvlist_lookup_uint64(*config, ZPOOL_CONFIG_POOL_TXG,
		    &txg) != 0 || txg == 0) {
			nvlist_free(*config);
			*config = NULL;
			continue;
		}

		break;
	}

	kmem_free(label, sizeof (vdev_label_t));
	device_close(dev);
	if (*config == NULL)
		error = EIDRM;

	return (error);
}
