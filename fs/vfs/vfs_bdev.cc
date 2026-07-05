/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <osv/device.h>
#include <osv/prex.h>
#include <osv/buf.h>
#include <osv/bio.h>

/*
 * Can this whole transfer take the fast path? That requires the starting
 * offset and every iovec length to be a whole multiple of BSIZE, because the
 * block drivers address and transfer in whole sectors. The buffer address
 * itself need not be aligned: virtio-blk (and the other block drivers) DMA
 * arbitrary guest addresses. Any unaligned offset or sub-sector length falls
 * back to the buffer-cache slow path.
 */
static bool
bdev_uio_sector_aligned(struct uio *uio)
{
	if ((uio->uio_offset % BSIZE) != 0)
		return false;
	for (int i = 0; i < uio->uio_iovcnt; i++) {
		if ((uio->uio_iov[i].iov_len % BSIZE) != 0)
			return false;
	}
	return true;
}

/*
 * Fast path: issue one bio per iovec straight through the device's strategy
 * (multiplex_strategy for real block devices, which splits by max_io_size and
 * dispatches all children before waiting). Because make_request only holds the
 * driver lock across ring submit, N concurrent callers keep N requests in
 * flight -- unlike the buffer-cache path, which serialized every 512-byte block
 * behind a single global lock and one synchronous bio.
 *
 * strategy() adds dev->offset exactly once (partition base), mirroring the
 * rw_buf()->strategy() path this replaces, so partition addressing is
 * unchanged.
 */
static int
bdev_strategy_rw(struct device *dev, struct uio *uio, int bio_cmd)
{
	while (uio->uio_resid > 0 && uio->uio_iovcnt > 0) {
		struct iovec *iov = uio->uio_iov;

		if (iov->iov_len == 0) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}

		struct bio *bio = alloc_bio();
		if (!bio)
			return ENOMEM;

		bio->bio_cmd = bio_cmd;
		bio->bio_dev = dev;
		bio->bio_data = iov->iov_base;
		bio->bio_offset = uio->uio_offset;
		bio->bio_bcount = iov->iov_len;

		dev->driver->devops->strategy(bio);
		int ret = bio_wait(bio);
		destroy_bio(bio);
		if (ret)
			return ret;

		uio->uio_offset += iov->iov_len;
		uio->uio_resid -= iov->iov_len;
		uio->uio_iov++;
		uio->uio_iovcnt--;
	}

	return 0;
}

int
bdev_read(struct device *dev, struct uio *uio, int ioflags)
{
	struct buf *bp;
	int ret;

	assert(uio->uio_rw == UIO_READ);

	if (uio->uio_offset + uio->uio_resid > dev->size)
		return EIO;
	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;

	if (bdev_uio_sector_aligned(uio))
		return bdev_strategy_rw(dev, uio, BIO_READ);

	/*
	 * Slow path: unaligned offset or sub-sector length. Read a whole BSIZE
	 * block through the buffer cache and copy from the correct byte offset
	 * within it. uiomove clamps each copy to min(remaining-in-block,
	 * uio_resid), so partial leading and trailing sectors are handled.
	 */
	while (uio->uio_resid > 0) {
		off_t blkno = uio->uio_offset >> 9;
		size_t off_in_blk = uio->uio_offset & (BSIZE - 1);

		ret = bread(dev, blkno, &bp);
		if (ret)
			return ret;

		ret = uiomove((char *)bp->b_data + off_in_blk,
			      BSIZE - off_in_blk, uio);
		brelse(bp);

		if (ret)
			return ret;
	}

	return 0;
}

int
bdev_write(struct device *dev, struct uio *uio, int ioflags)
{
	struct buf *bp;
	int ret;

	assert(uio->uio_rw == UIO_WRITE);

	if (uio->uio_offset + uio->uio_resid > dev->size)
		return EIO;
	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;

	if (bdev_uio_sector_aligned(uio))
		return bdev_strategy_rw(dev, uio, BIO_WRITE);

	/*
	 * Slow path: unaligned offset or sub-sector length. Read-modify-write
	 * each affected BSIZE block through the buffer cache so the bytes
	 * outside the written range are preserved. (getblk alone returns an
	 * unread buffer, which would corrupt the untouched remainder of the
	 * sector; bread reads it first.)
	 */
	while (uio->uio_resid > 0) {
		off_t blkno = uio->uio_offset >> 9;
		size_t off_in_blk = uio->uio_offset & (BSIZE - 1);

		ret = bread(dev, blkno, &bp);
		if (ret)
			return ret;

		ret = uiomove((char *)bp->b_data + off_in_blk,
			      BSIZE - off_in_blk, uio);
		if (ret) {
			brelse(bp);
			return ret;
		}

		ret = bwrite(bp);
		if (ret)
			return ret;
	}

	return 0;
}


int
physio(struct device *dev, struct uio *uio, int ioflags)
{
	struct bio *bio;
	int ret;

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;
    
	while (uio->uio_resid > 0) {
		struct iovec *iov = uio->uio_iov;

		if (!iov->iov_len)
			continue;

		bio = alloc_bio();
		if (!bio)
			return ENOMEM;

		if (uio->uio_rw == UIO_READ)
			bio->bio_cmd = BIO_READ;
		else
			bio->bio_cmd = BIO_WRITE;

		bio->bio_dev = dev;
		bio->bio_data = iov->iov_base;
		bio->bio_offset = uio->uio_offset;
		bio->bio_bcount = uio->uio_resid;

		dev->driver->devops->strategy(bio);

		ret = bio_wait(bio);
		destroy_bio(bio);
		if (ret)
			return ret;

	        uio->uio_iov++;
        	uio->uio_iovcnt--;
        	uio->uio_resid -= iov->iov_len;
        	uio->uio_offset += iov->iov_len;
	}

	return 0;
}
