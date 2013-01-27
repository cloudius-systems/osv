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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#include <../../fs/vfs/uio.h>
#include <../../fs/devfs/device.h>

struct ramdisk_softc {
	char		*addr;		/* base address of image */
	size_t		size;		/* image size */
};

static int
ramdisk_rdwr(struct device *dev, struct uio *uio, int ioflags)
{
	struct ramdisk_softc *sc = dev->private_data;
	size_t len;

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;
	if (uio->uio_offset > sc->size)
		return EIO;

	if (sc->size - uio->uio_offset < uio->uio_resid)
		len = sc->size - uio->uio_offset;
	else
		len = uio->uio_resid;

	return uiomove(sc->addr + uio->uio_offset, len, uio);
}

static struct devops ramdisk_devops = {
	.open		= no_open,
	.close		= no_close,
	.read		= ramdisk_rdwr,
	.write		= ramdisk_rdwr,
	.ioctl		= no_ioctl,
	.devctl		= no_devctl,
};

struct driver ramdisk_driver = {
	.name		= "ramdisk",
	.devops		= &ramdisk_devops,
	.devsz		= sizeof(struct ramdisk_softc),
};

int
ramdisk_init(void)
{
	struct ramdisk_softc *sc;
	struct device *dev;

	dev = device_create(&ramdisk_driver, "ram0", D_BLK);
	sc = dev->private_data;
	sc->size = 4 * 1024 * 1024;
	sc->addr = malloc(sc->size);

	printf("RAM disk at 0x%08x (%dK bytes)\n",
	       (unsigned long)sc->addr, sc->size/1024);
	return 0;
}
