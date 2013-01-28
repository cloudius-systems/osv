
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <osv/device.h>
#include "bio.h"

static void
physio_done(struct bio *bio)
{
}

int
physio(struct device *dev, struct uio *uio, int ioflags)
{
	struct bio *bio;

	if (uio->uio_offset < 0)
		return EINVAL;
	if (uio->uio_resid == 0)
		return 0;
    
	while (uio->uio_resid > 0) {
		struct iovec *iov = uio->uio_iov;

		if (!iov->iov_len)
			continue;

		bio = malloc(sizeof(*bio));
		if (!bio)
			return ENOMEM;
		memset(bio, 0, sizeof(*bio));
		if (uio->uio_rw == UIO_READ)
			bio->bio_cmd = BIO_READ;
		else
			bio->bio_cmd = BIO_WRITE;

		bio->bio_dev = dev;
		bio->bio_done = physio_done;

		bio->bio_data = iov->iov_base;
		bio->bio_offset = uio->uio_offset;
		bio->bio_bcount = uio->uio_resid;

		dev->driver->devops->strategy(bio);
		free(bio);

	        uio->uio_iov++;
        	uio->uio_iovcnt--;
        	uio->uio_resid -= iov->iov_len;
        	uio->uio_offset += iov->iov_len;
	}

	return 0;
}

