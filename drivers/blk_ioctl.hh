#ifndef BLK_IOCTL_H
#define BLK_IOCTL_H

#define _IOC_NRBITS     8
#define _IOC_TYPEBITS   8
#define _IOC_SIZEBITS   13
#define _IOC_DIRBITS    3

#define _IOC_NRMASK     ((1 << _IOC_NRBITS)-1)
#define _IOC_TYPEMASK   ((1 << _IOC_TYPEBITS)-1)
#define _IOC_SIZEMASK   ((1 << _IOC_SIZEBITS)-1)
#define _IOC_DIRMASK    ((1 << _IOC_DIRBITS)-1)

#define _IOC_NRSHIFT    0
#define _IOC_TYPESHIFT  (_IOC_NRSHIFT+_IOC_NRBITS)
#define _IOC_SIZESHIFT  (_IOC_TYPESHIFT+_IOC_TYPEBITS)
#define _IOC_DIRSHIFT   (_IOC_SIZESHIFT+_IOC_SIZEBITS)

#define _IOC_DIR(nr)    (((nr) >> _IOC_DIRSHIFT) & _IOC_DIRMASK)
#define _IOC_TYP(nr)   (((nr) >> _IOC_TYPESHIFT) & _IOC_TYPEMASK)
#define _IOC_NR(nr)     (((nr) >> _IOC_NRSHIFT) & _IOC_NRMASK)
#define _IOC_SIZE(nr)   (((nr) >> _IOC_SIZESHIFT) & _IOC_SIZEMASK)

#define BLKGETSIZE64    114
#define BLKFLSBUF       97
#define BLKDISCARD      119

TRACEPOINT(trace_blk_ioctl, "dev=%s type=%#x nr=%d size=%d, dir=%d", char*, int, int, int, int);

void no_bio_done(bio * b) {delete b;};

int
blk_ioctl(struct device* dev, u_long io_cmd, void* buf)
{
    assert(dev);
    trace_blk_ioctl(dev->name, _IOC_TYP(io_cmd), _IOC_NR(io_cmd), _IOC_SIZE(io_cmd), _IOC_DIR(io_cmd));

    switch (_IOC_NR(io_cmd)) {
        case BLKGETSIZE64:
            //device capacity in bytes
            *(off_t*) buf = dev->size;
            break;
        case BLKFLSBUF: {
            auto* bio = alloc_bio();
            bio->bio_dev = dev;
            bio->bio_done = no_bio_done;
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

#endif