/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sstream>
#include <drivers/xenfront.hh>
#include <osv/device.h>
#include <bsd/sys/geom/geom_disk.h>
#include <osv/bio.h>

extern "C" {

struct xb_softc;
struct device *blkfront_from_softc(struct xb_softc *s);

struct xenfront_blk_priv {
    devop_strategy_t strategy;
    xenfront::xenfront_driver *drv;
};

static int
xenfront_blk_read(struct device *dev, struct uio *uio, int ioflags)
{
    if (uio->uio_offset + uio->uio_resid > dev->size)
        return EIO;

    int x = bdev_read(dev, uio, ioflags);
    return x;
}

static int
xenfront_blk_write(struct device *dev, struct uio *uio, int ioflags)
{
    if (uio->uio_offset + uio->uio_resid > dev->size)
        return EIO;

    return bdev_write(dev, uio, ioflags);
}

static struct devops xenfront_blk_devops {
    no_open,
    no_close,
    xenfront_blk_read,
    xenfront_blk_write,
    no_ioctl,
    no_devctl,
    multiplex_strategy,
};

struct driver xenfront_blk_driver = {
    "xenfront_blk",
    &xenfront_blk_devops,
    sizeof(struct xenfront_blk_priv),
};

void disk_create(struct disk *dp, int version)
{
    struct xb_softc *sc = static_cast<struct xb_softc *>(dp->d_drv1);
    struct device *dev = blkfront_from_softc(sc);
    xenfront::xenfront_driver *blkfront = xenfront::xenfront_driver::from_device(dev);

    std::stringstream name; 
    name << blkfront->get_name();
    name << dp->d_unit;

    dev->driver = &xenfront_blk_driver;
    device_register(dev, name.str().c_str(), D_BLK);

    struct xenfront_blk_priv *prv;
    prv = static_cast<struct xenfront_blk_priv*>(dev->private_data);
    prv->drv = blkfront;
    prv->strategy = dp->d_strategy;

    dev->size = dp->d_mediasize;
    dev->max_io_size = dp->d_maxsize;
}
};
