/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

/*-
 * Copyright (c) 2006, Kohsuke Ohtani
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

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <osv/device.h>
#include <osv/bio.h>

struct ramdisk_softc {
	char		*addr;		/* base address of image */
	size_t		size;		/* image size */
	TAILQ_HEAD(, bio) bio_queue;
	pthread_mutex_t	bio_mutex;
	pthread_cond_t	bio_wait;
};


static pthread_t ramdisk_thread;

static int
ramdisk_read(struct device *dev, struct uio *uio, int ioflags)
{
	struct ramdisk_softc *sc = dev->private_data;

	if (uio->uio_offset + uio->uio_resid > sc->size)
		return EIO;

	return bdev_read(dev, uio, ioflags);
}

static int
ramdisk_write(struct device *dev, struct uio *uio, int ioflags)
{
	struct ramdisk_softc *sc = dev->private_data;

	if (uio->uio_offset + uio->uio_resid > sc->size)
		return EIO;

	return bdev_write(dev, uio, ioflags);
}

static void
ramdisk_io(struct ramdisk_softc *sc, struct bio *bio)
{
	switch (bio->bio_cmd) {
	case BIO_READ:
		memcpy(bio->bio_data, sc->addr + bio->bio_offset,
		       bio->bio_bcount);
		break;
	case BIO_WRITE:
		memcpy(sc->addr + bio->bio_offset, bio->bio_data,
		       bio->bio_bcount);
		break;
	default:
		assert(0);
	}

	biodone(bio, true);
}

static void
ramdisk_strategy(struct bio *bio)
{
	struct ramdisk_softc *sc = bio->bio_dev->private_data;

	pthread_mutex_lock(&sc->bio_mutex);
	TAILQ_INSERT_TAIL(&sc->bio_queue, bio, bio_queue);
	pthread_cond_signal(&sc->bio_wait);
	pthread_mutex_unlock(&sc->bio_mutex);
}

static struct devops ramdisk_devops = {
	.open		= no_open,
	.close		= no_close,
	.read		= ramdisk_read,
	.write		= ramdisk_write,
	.ioctl		= no_ioctl,
	.devctl		= no_devctl,
	.strategy	= ramdisk_strategy,
};

struct driver ramdisk_driver = {
	.name		= "ramdisk",
	.devops		= &ramdisk_devops,
	.devsz		= sizeof(struct ramdisk_softc),
};

static void *ramdisk_thread_fn(void *arg)
{
	struct ramdisk_softc *sc = arg;

	pthread_mutex_lock(&sc->bio_mutex);
	for (;;) {
		while (!TAILQ_EMPTY(&sc->bio_queue)) {
			struct bio *bio = TAILQ_FIRST(&sc->bio_queue);

			TAILQ_REMOVE(&sc->bio_queue, bio, bio_queue);
			ramdisk_io(sc, bio);
		}
		pthread_cond_wait(&sc->bio_wait, &sc->bio_mutex);
	}
	pthread_mutex_unlock(&sc->bio_mutex);

	return NULL;
}

int
ramdisk_init(void)
{
	struct ramdisk_softc *sc;
	struct device *dev;
	int error;

	dev = device_create(&ramdisk_driver, "ram0", D_BLK);
	sc = dev->private_data;
	sc->size = dev->size = 4 * 1024 * 1024;
	sc->addr = malloc(sc->size);

	TAILQ_INIT(&sc->bio_queue);
	pthread_mutex_init(&sc->bio_mutex, NULL);
	pthread_cond_init(&sc->bio_wait, NULL);

	printf("RAM disk at 0x%08lx (%zdK bytes)\n",
	       (unsigned long)sc->addr, sc->size/1024);
    
	error = pthread_create(&ramdisk_thread, NULL, ramdisk_thread_fn, sc);
	if (error) {
		fprintf(stderr, "failes to create ramdisk thread: %s\n",
			strerror(error));
	}
	return 0;
}
