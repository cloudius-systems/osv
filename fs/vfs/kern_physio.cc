/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <new>

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/export.h>
#include <sys/param.h>
#include <assert.h>
#include <sys/refcount.h>
#include <osv/mutex.h>
#include <osv/waitqueue.hh>
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <osv/fork_arena.hh>
#endif

OSV_LIBSOLARIS_API struct bio *
alloc_bio(void)
{
#if CONF_fork
	// A bio is a kernel I/O descriptor handed off across the fork COW address
	// space boundary: a forked process (e.g. PostgreSQL's startup process doing
	// recovery/checkpoint on ZFS) allocates it here while issuing a synchronous
	// read/write, but the SINGLE block-device completion thread (virtio-blk
	// req_done, running in AS0) reads it back -- bio_done, bio_caller1 (the
	// zio), bio_flags -- to complete the I/O.  If the bio landed in the per-AS
	// COW fork arena, the AS0 completion thread would read a DIFFERENT physical
	// copy than the submitter wrote: it would fetch a stale bio_caller1/bio_done
	// and drive completion (zio_interrupt -> zio_done -> cv_broadcast) against
	// the wrong zio, so the real waiter's zio_wait() is never woken -- all vCPUs
	// go idle and PostgreSQL never reaches "ready to accept connections".
	// Allocate the bio on the identity kernel heap (mapped verbatim in every
	// address space) so the submitter and the AS0 completion thread share ONE
	// coherent bio.  Same rule as the shipped virtio-blk blk_req / virtio-net
	// net_req cross-AS fixes.
	fork_arena::kernel_heap_scope kh;
#endif
	auto *b = new (std::nothrow) bio();
	if (!b)
		return nullptr;
	return b;
}

OSV_LIBSOLARIS_API void
destroy_bio(struct bio *bio)
{
	delete bio;
}

OSV_LIBSOLARIS_API int
bio_wait(struct bio *bio)
{
	SCOPE_LOCK(bio->bio_mutex);
	while (!(bio->bio_flags & BIO_DONE)) {
		bio->bio_wait.wait(bio->bio_mutex);
	}
	if (bio->bio_flags & BIO_ERROR) {
		return EIO;
	}
	return 0;
}

void
biodone(struct bio *bio, bool ok)
{
	WITH_LOCK(bio->bio_mutex) {
		bio->bio_flags |= BIO_DONE;
		if (!ok)
			bio->bio_flags |= BIO_ERROR;
		if (!bio->bio_done) {
			bio->bio_wait.wake_one(bio->bio_mutex);
			return;
		}
	}
	bio->bio_done(bio);
}

void
biofinish(struct bio *bp, struct devstat *stat, int error)
{
	if (error) {
		bp->bio_error = error;
	}
	biodone(bp, error);
}

static void multiplex_bio_done(struct bio *b)
{
	struct bio *bio = static_cast<struct bio*>(b->bio_caller1);
	bool error = b->bio_flags & BIO_ERROR;
	destroy_bio(b);


	// If there is an error, we store it in the original bio flags.
	// This path gets slower because then we need to end up taking the
	// bio_mutex twice. But that should be fine.
	if (error) {
		WITH_LOCK(bio->bio_mutex) {
			bio->bio_flags |= BIO_ERROR;
		}
	}

	// Last one releases it. We set the biodone to always be "ok", because
	// if an error exists, we have already set that in the previous operation
	if (refcount_release(&bio->bio_refcnt))
		biodone(bio, true);
}

void multiplex_strategy(struct bio *bio)
{
	struct device *dev = bio->bio_dev;
	devop_strategy_t strategy = *((devop_strategy_t *)dev->private_data);

	uint64_t len = bio->bio_bcount;

	bio->bio_offset += bio->bio_dev->offset;
	uint64_t offset = bio->bio_offset;
	void *buf = bio->bio_data;

	assert(strategy != nullptr);

	// A discard request carries no data payload (bio_data is nullptr) even
	// though bio_bcount is non-zero, so the max_io_size data-segment limit
	// does not apply and splitting it would do pointer arithmetic on nullptr.
	// Forward it whole; the driver enforces its own discard-size limit.
	if (bio->bio_cmd == BIO_DISCARD || len <= dev->max_io_size) {
		strategy(bio);
		return;
	}

	// It is better to initialize the refcounter beforehand, specially because we can
	// trivially determine what is the number going to be. Otherwise, we can have a
	// situation in which we bump the refcount to 1, get scheduled out, the bio is
	// finished, and when it drops its refcount to 0, we consider the main bio finished.
	refcount_init(&bio->bio_refcnt, (len / dev->max_io_size) + !!(len % dev->max_io_size));

	while (len > 0) {
		uint64_t req_size = MIN(len, dev->max_io_size);
		struct bio *b = alloc_bio();

		b->bio_bcount = req_size;
		b->bio_data = buf;
		b->bio_offset = offset;

		b->bio_cmd = bio->bio_cmd;
		b->bio_dev = bio->bio_dev;
		b->bio_caller1 = bio;
		b->bio_private = bio->bio_private;
		b->bio_done = multiplex_bio_done;

		strategy(b);
		buf += req_size;
		offset += req_size;
		len -= req_size;
	}
}
