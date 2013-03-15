/*
 * Copyright (c) 2005-2007, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * vfs_bio.c - buffered I/O operations
 */

/*
 * References:
 *	Bach: The Design of the UNIX Operating System (Prentice Hall, 1986)
 */

#include <osv/prex.h>
#include <osv/list.h>
#include <osv/buf.h>
#include <osv/bio.h>
#include <osv/device.h>

#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>

#include "vfs.h"

/* number of buffer cache */
#define NBUFS		256

/* macros to clear/set/test flags. */
#define	SET(t, f)	(t) |= (f)
#define	CLR(t, f)	(t) &= ~(f)
#define	ISSET(t, f)	((t) & (f))

/*
 * Global lock to access all buffer headers and lists.
 */
static mutex_t bio_lock = MUTEX_INITIALIZER;
#define BIO_LOCK()	mutex_lock(&bio_lock)
#define BIO_UNLOCK()	mutex_unlock(&bio_lock)


/* fixed set of buffers */
static struct buf buf_table[NBUFS];
static struct list_head free_list = LIST_SINIT(free_list);

static sem_t free_sem;

/*
 * Insert buffer to the head of free list
 */
static void
bio_insert_head(struct buf *bp)
{

	list_insert(&free_list, &bp->b_link);
	sem_post(&free_sem);
}

/*
 * Insert buffer to the tail of free list
 */
static void
bio_insert_tail(struct buf *bp)
{

	list_insert(list_prev(&free_list), &bp->b_link);
	sem_post(&free_sem);
}

/*
 * Remove buffer from free list
 */
static void
bio_remove(struct buf *bp)
{
	sem_wait(&free_sem);
	ASSERT(!list_empty(&free_list));
	list_remove(&bp->b_link);
}

/*
 * Remove buffer from the head of free list
 */
static struct buf *
bio_remove_head(void)
{
	struct buf *bp;

	sem_wait(&free_sem);
	ASSERT(!list_empty(&free_list));
	bp = list_entry(list_first(&free_list), struct buf, b_link);
	list_remove(&bp->b_link);
	return bp;
}


static int
rw_buf(struct buf *bp, int rw)
{
	struct bio *bio;
	int ret;

	bio = alloc_bio();
	if (!bio)
		return ENOMEM;

	bio->bio_cmd = rw ? BIO_WRITE : BIO_READ;
	bio->bio_dev = bp->b_dev;
	bio->bio_data = bp->b_data;
	bio->bio_offset = bp->b_blkno << 9;
	bio->bio_bcount = BSIZE;

	bio->bio_dev->driver->devops->strategy(bio);
	ret = bio_wait(bio);

	destroy_bio(bio);
	return ret;
}

/*
 * Determine if a block is in the cache.
 */
static struct buf *
incore(struct device *dev, int blkno)
{
	struct buf *bp;
	int i;

	for (i = 0; i < NBUFS; i++) {
		bp = &buf_table[i];
		if (bp->b_blkno == blkno && bp->b_dev == dev &&
		    !ISSET(bp->b_flags, B_INVAL))
			return bp;
	}
	return NULL;
}

/*
 * Assign a buffer for the given block.
 *
 * The block is selected from the buffer list with LRU
 * algorithm.  If the appropriate block already exists in the
 * block list, return it.  Otherwise, the least recently used
 * block is used.
 */
struct buf *
getblk(struct device *dev, int blkno)
{
	struct buf *bp;

	DPRINTF(VFSDB_BIO, ("getblk: dev=%x blkno=%d\n", dev, blkno));
 start:
	BIO_LOCK();
	bp = incore(dev, blkno);
	if (bp != NULL) {
		/* Block found in cache. */
		if (ISSET(bp->b_flags, B_BUSY)) {
			/*
			 * Wait buffer ready.
			 */
			BIO_UNLOCK();
			mutex_lock(&bp->b_lock);
			mutex_unlock(&bp->b_lock);
			/* Scan again if it's busy */
			goto start;
		}
		bio_remove(bp);
		SET(bp->b_flags, B_BUSY);
	} else {
		bp = bio_remove_head();
		if (ISSET(bp->b_flags, B_DELWRI)) {
			BIO_UNLOCK();
			bwrite(bp);
			goto start;
		}
		bp->b_flags = B_BUSY;
		bp->b_dev = dev;
		bp->b_blkno = blkno;
	}
	mutex_lock(&bp->b_lock);
	BIO_UNLOCK();
	DPRINTF(VFSDB_BIO, ("getblk: done bp=%x\n", bp));
	return bp;
}

/*
 * Release a buffer, with no I/O implied.
 */
void
brelse(struct buf *bp)
{
	ASSERT(ISSET(bp->b_flags, B_BUSY));
	DPRINTF(VFSDB_BIO, ("brelse: bp=%x dev=%x blkno=%d\n",
				bp, bp->b_dev, bp->b_blkno));

	BIO_LOCK();
	CLR(bp->b_flags, B_BUSY);
	mutex_unlock(&bp->b_lock);
	if (ISSET(bp->b_flags, B_INVAL))
		bio_insert_head(bp);
	else
		bio_insert_tail(bp);
	BIO_UNLOCK();
}

/*
 * Block read with cache.
 * @dev:   device id to read from.
 * @blkno: block number.
 * @buf:   buffer pointer to be returned.
 *
 * An actual read operation is done only when the cached
 * buffer is dirty.
 */
int
bread(struct device *dev, int blkno, struct buf **bpp)
{
	struct buf *bp;
	size_t size;
	int error;

	DPRINTF(VFSDB_BIO, ("bread: dev=%x blkno=%d\n", dev, blkno));
	bp = getblk(dev, blkno);

	if (!ISSET(bp->b_flags, (B_DONE | B_DELWRI))) {
		size = BSIZE;
		error = rw_buf(bp, 0);
		if (error) {
			DPRINTF(VFSDB_BIO, ("bread: i/o error\n"));
			brelse(bp);
			return error;
		}
	}
	CLR(bp->b_flags, B_INVAL);
	SET(bp->b_flags, (B_READ | B_DONE));
	DPRINTF(VFSDB_BIO, ("bread: done bp=%x\n\n", bp));
	*bpp = bp;
	return 0;
}

/*
 * Block write with cache.
 * @buf:   buffer to write.
 *
 * The data is copied to the buffer.
 * Then release the buffer.
 */
int
bwrite(struct buf *bp)
{
	size_t size;
	int error;

	ASSERT(ISSET(bp->b_flags, B_BUSY));
	DPRINTF(VFSDB_BIO, ("bwrite: dev=%x blkno=%d\n", bp->b_dev,
			    bp->b_blkno));

	BIO_LOCK();
	CLR(bp->b_flags, (B_READ | B_DONE | B_DELWRI));
	BIO_UNLOCK();

	size = BSIZE;
	error = rw_buf(bp, 1);
	if (error)
		return error;
	BIO_LOCK();
	SET(bp->b_flags, B_DONE);
	BIO_UNLOCK();
	brelse(bp);
	return 0;
}

/*
 * Delayed write.
 *
 * The buffer is marked dirty, but an actual I/O is not
 * performed.  This routine should be used when the buffer
 * is expected to be modified again soon.
 */
void
bdwrite(struct buf *bp)
{

	BIO_LOCK();
	SET(bp->b_flags, B_DELWRI);
	CLR(bp->b_flags, B_DONE);
	BIO_UNLOCK();
	brelse(bp);
}

/*
 * Flush write-behind block
 */
void
bflush(struct buf *bp)
{

	BIO_LOCK();
	if (ISSET(bp->b_flags, B_DELWRI))
		bwrite(bp);
	BIO_UNLOCK();
}

/*
 * Invalidate buffer for specified device.
 * This is called when unmount.
 */
void
binval(struct device *dev)
{
	struct buf *bp;
	int i;

	BIO_LOCK();
	for (i = 0; i < NBUFS; i++) {
		bp = &buf_table[i];
		if (bp->b_dev == dev) {
			if (ISSET(bp->b_flags, B_DELWRI))
				bwrite(bp);
			else if (ISSET(bp->b_flags, B_BUSY))
				brelse(bp);
			bp->b_flags = B_INVAL;
		}
	}
	BIO_UNLOCK();
}

/*
 * Invalidate all buffers.
 * This is called when unmount.
 */
void
bio_sync(void)
{
	struct buf *bp;
	int i;

 start:
	BIO_LOCK();
	for (i = 0; i < NBUFS; i++) {
		bp = &buf_table[i];
		if (ISSET(bp->b_flags, B_BUSY)) {
			BIO_UNLOCK();
			mutex_lock(&bp->b_lock);
			mutex_unlock(&bp->b_lock);
			goto start;
		}
		if (ISSET(bp->b_flags, B_DELWRI))
			bwrite(bp);
	}
	BIO_UNLOCK();
}

/*
 * Initialize the buffer I/O system.
 */
void
bio_init(void)
{
	struct buf *bp;
	int i;

	for (i = 0; i < NBUFS; i++) {
		bp = &buf_table[i];
		bp->b_flags = B_INVAL;
		bp->b_data = malloc(BSIZE);
		mutex_init(&bp->b_lock);
		list_insert(&free_list, &bp->b_link);
	}
	sem_init(&free_sem, 0, NBUFS);

	DPRINTF(VFSDB_BIO, ("bio: Buffer cache size %dK bytes\n",
			    BSIZE * NBUFS / 1024));
}
