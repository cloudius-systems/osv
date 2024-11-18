/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/types.h>
#include <osv/device.h>
#include <osv/bio.h>
#include <osv/ioctl.h>

#include <osv/trace.hh>
#include "drivers/blk-common.hh"

#include <sys/mount.h>

TRACEPOINT(trace_blk_ioctl, "dev=%s type=%#x nr=%d size=%d, dir=%d", char*, int, int, int, int);

int
blk_ioctl(struct device* dev, u_long io_cmd, void* buf)
{
    assert(dev);
    trace_blk_ioctl(dev->name, _IOC_TYP(io_cmd), _IOC_NR(io_cmd), _IOC_SIZE(io_cmd), _IOC_DIR(io_cmd));

    switch (io_cmd) {
        case BLKGETSIZE64:
            //device capacity in bytes
            if (!buf) {
                return EINVAL;
            }
            *(off_t*) buf = dev->size;
            break;
        case BLKFLSBUF:
            {
                auto* bio = alloc_bio();
                bio->bio_dev = dev;
                bio->bio_done = destroy_bio;
                bio->bio_cmd = BIO_FLUSH;

                dev->driver->devops->strategy(bio);
            }
            break;
        default:
            printf("ioctl not defined; type:%#x nr:%d size:%d, dir:%d\n",_IOC_TYP(io_cmd),_IOC_NR(io_cmd),_IOC_SIZE(io_cmd),_IOC_DIR(io_cmd));
            return EINVAL;
    }
    return 0;
}
