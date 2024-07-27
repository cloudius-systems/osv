/*
 * XenBSD block device driver
 *
 * Copyright (c) 2009 Scott Long, Yahoo!
 * Copyright (c) 2009 Frank Suchomel, Citrix
 * Copyright (c) 2009 Doug F. Rabson, Citrix
 * Copyright (c) 2005 Kip Macy
 * Copyright (c) 2003-2004, Keir Fraser & Steve Hand
 * Modifications by Mark A. Williamson are (c) Intel Research Cambridge
 *
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define __STDC_FORMAT_MACROS

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <osv/mempool.hh>
#include <osv/contiguous_alloc.hh>
#include <bsd/porting/netport.h>
#include <bsd/porting/synch.h>
#include <bsd/porting/bus.h>
#include <bsd/porting/mmu.h>
#include <bsd/porting/kthread.h>

#include <bsd/porting/netport.h>
#include <bsd/porting/synch.h>
#include <bsd/porting/bus.h>
#include <bsd/porting/mmu.h>
#include <bsd/porting/kthread.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <vm/vm.h>
#include <vm/pmap.h>

#include <sys/bio.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>
#include <machine/intr_machdep.h>
#include <machine/vmparam.h>
#include <sys/bus_dma.h>

#include <machine/_inttypes.h>
#include <machine/xen/xen-os.h>
#include <machine/xen/xenvar.h>

#include <xen/hypervisor.h>
#include <xen/xen_intr.h>
#include <xen/evtchn.h>
#include <xen/gnttab.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/io/protocols.h>
#include <xen/xenbus/xenbusvar.h>

#include <geom/geom_disk.h>

#include <dev/xen/blkfront/block.h>

#include "xenbus_if.h"

#include <stack>

/* prototypes */
static void xb_free_command(struct xb_command *cm);
static void xb_startio(struct xb_softc *sc);
static void blkfront_connect(struct xb_softc *);
static void blkfront_closing(device_t);
static int blkfront_detach(device_t);
static int setup_blkring(struct xb_softc *);
static void blkif_int(void *);
static void blkfront_initialize(struct xb_softc *);
static int blkif_completion(struct xb_command *);
static void blkif_free(struct xb_softc *);
static void blkif_queue_cb(void *, bus_dma_segment_t *, int, int);

#define GRANT_INVALID_REF 0

/* Control whether runtime update of vbds is enabled. */
#define ENABLE_VBD_UPDATE 0

#if ENABLE_VBD_UPDATE
static void vbd_update(void);
#endif

#define BLKIF_STATE_DISCONNECTED 0
#define BLKIF_STATE_CONNECTED    1
#define BLKIF_STATE_SUSPENDED    2

#ifdef notyet
static char *blkif_state_name[] = {
    [BLKIF_STATE_DISCONNECTED] = "disconnected",
    [BLKIF_STATE_CONNECTED]    = "connected",
    [BLKIF_STATE_SUSPENDED]    = "closed",
};

static char * blkif_status_name[] = {
    [BLKIF_INTERFACE_STATUS_CLOSED]       = "closed",
    [BLKIF_INTERFACE_STATUS_DISCONNECTED] = "disconnected",
    [BLKIF_INTERFACE_STATUS_CONNECTED]    = "connected",
    [BLKIF_INTERFACE_STATUS_CHANGED]      = "changed",
};
#endif

#if 0
#define DPRINTK(fmt, args...) printf("[XEN] %s:%d: " fmt ".\n", __func__, __LINE__, ##args)
#else
#define DPRINTK(fmt, args...)
#endif

static int blkif_open(struct disk *dp);
static int blkif_close(struct disk *dp);
static int blkif_ioctl(struct disk *dp, u_long cmd, void *addr, int flag, struct thread *td);
static int blkif_queue_request(struct xb_softc *sc, struct xb_command *cm);
static void xb_quiesce(struct xb_softc *sc);
static void xb_strategy(struct bio *bp);

// In order to quiesce the device during kernel dumps, outstanding requests to
// DOM0 for disk reads/writes need to be accounted for.
static    int    xb_dump(void *, void *, vm_offset_t, off_t, size_t);

/* XXX move to xb_vbd.c when VBD update support is added */
#define MAX_VBDS 64

#define XBD_SECTOR_SIZE        512    /* XXX: assume for now */
#define XBD_SECTOR_SHFT        9

/*
 * Translate Linux major/minor to an appropriate name and unit
 * number. For HVM guests, this allows us to use the same drive names
 * with blkfront as the emulated drives, easing transition slightly.
 */
static void
blkfront_vdevice_to_unit(uint32_t vdevice, int *unit, const char **name)
{
    static struct vdev_info {
        int major;
        int shift;
        int base;
        const char *name;
    } info[] = {
        {3,    6,    0,    "vblk"},    /* ide0 */
        {22,    6,    2,    "vblk"},    /* ide1 */
        {33,    6,    4,    "vblk"},    /* ide2 */
        {34,    6,    6,    "vblk"},    /* ide3 */
        {56,    6,    8,    "vblk"},    /* ide4 */
        {57,    6,    10,    "vblk"},    /* ide5 */
        {88,    6,    12,    "vblk"},    /* ide6 */
        {89,    6,    14,    "vblk"},    /* ide7 */
        {90,    6,    16,    "vblk"},    /* ide8 */
        {91,    6,    18,    "vblk"},    /* ide9 */

        {8,    4,    0,    "da"},    /* scsi disk0 */
        {65,    4,    16,    "da"},    /* scsi disk1 */
        {66,    4,    32,    "da"},    /* scsi disk2 */
        {67,    4,    48,    "da"},    /* scsi disk3 */
        {68,    4,    64,    "da"},    /* scsi disk4 */
        {69,    4,    80,    "da"},    /* scsi disk5 */
        {70,    4,    96,    "da"},    /* scsi disk6 */
        {71,    4,    112,    "da"},    /* scsi disk7 */
        {128,    4,    128,    "da"},    /* scsi disk8 */
        {129,    4,    144,    "da"},    /* scsi disk9 */
        {130,    4,    160,    "da"},    /* scsi disk10 */
        {131,    4,    176,    "da"},    /* scsi disk11 */
        {132,    4,    192,    "da"},    /* scsi disk12 */
        {133,    4,    208,    "da"},    /* scsi disk13 */
        {134,    4,    224,    "da"},    /* scsi disk14 */
        {135,    4,    240,    "da"},    /* scsi disk15 */

        {202,    4,    0,    "xbd"},    /* xbd */

        {0,    0,    0,    NULL},
    };
    int major = vdevice >> 8;
    int minor = vdevice & 0xff;
    int i;

    if (vdevice & (1 << 28)) {
        *unit = (vdevice & ((1 << 28) - 1)) >> 8;
        *name = "xbd";
        return;
    }

    for (i = 0; info[i].major; i++) {
        if (info[i].major == major) {
            *unit = info[i].base + (minor >> info[i].shift);
            *name = info[i].name;
            return;
        }
    }

    *unit = minor >> 4;
    *name = "xbd";
}

struct disk *disk_alloc(void)
{
    return (disk *)malloc(sizeof(struct disk) , M_WHATEVER, 0);
}

int
xlvbd_add(struct xb_softc *sc, blkif_sector_t sectors,
    int vdevice, uint16_t vdisk_info, unsigned long sector_size)
{
    int    unit, error = 0;
    const char *name;

    blkfront_vdevice_to_unit(vdevice, &unit, &name);

    sc->xb_unit = unit;

    if (strcmp(name, "xbd"))
        device_printf(sc->xb_dev, " attaching as %s%d\n", name, unit);

    sc->xb_disk = disk_alloc();
    sc->xb_disk->d_unit = sc->xb_unit;
    sc->xb_disk->d_open = blkif_open;
    sc->xb_disk->d_close = blkif_close;
    sc->xb_disk->d_ioctl = blkif_ioctl;
    sc->xb_disk->d_strategy = xb_strategy;
    sc->xb_disk->d_dump = xb_dump;
    sc->xb_disk->d_name = name;
    sc->xb_disk->d_drv1 = sc;
    sc->xb_disk->d_sectorsize = sector_size;

    sc->xb_disk->d_mediasize = sectors * sector_size;
    sc->xb_disk->d_maxsize = sc->max_request_size;
    sc->xb_disk->d_flags = 0;
    disk_create(sc->xb_disk, DISK_VERSION);

    return error;
}

/************************ end VBD support *****************/

/*
 * Read/write routine for a buffer.  Finds the proper unit, place it on
 * the sortq and kick the controller.
 */
static void
xb_strategy(struct bio *bp)
{
    struct xb_softc    *sc = (xb_softc *)bp->bio_dev->softc;
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);

    /* bogus disk? */
    if (sc == NULL) {
        bp->bio_error = EINVAL;
        bp->bio_resid = bp->bio_bcount;
        biodone(bp, false);
        return;
    }

    if ((bp->bio_cmd == BIO_FLUSH) &&
            !((sc->xb_flags & XB_BARRIER) || (sc->xb_flags & XB_FLUSH))) {
        bp->bio_error = EOPNOTSUPP;
        bp->bio_resid = bp->bio_bcount;
        biodone(bp, false);
        return;
    }

    /*
     * Place it in the queue of disk activities for this disk
     */
    mutex_lock(&xsc->xb_io_lock);

    xb_enqueue_bio(sc, bp);
    xb_startio(sc);

    mutex_unlock(&xsc->xb_io_lock);
    return;
}

static void
xb_bio_complete(struct xb_softc *sc, struct xb_command *cm)
{
    struct bio *bp;

    bp = cm->bp;

    if ( unlikely(cm->status != BLKIF_RSP_OKAY) ) {
        disk_err(bp, "disk error" , -1, 0);
        printf(" status: %x\n", cm->status);
        bp->bio_flags |= BIO_ERROR;
    }

    if (bp->bio_flags & BIO_ERROR)
        bp->bio_error = EIO;
    else
        bp->bio_resid = 0;

    xb_free_command(cm);
    biodone(bp, !(bp->bio_flags & BIO_ERROR));
}

// Quiesce the disk writes for a dump file before allowing the next buffer.
static void
xb_quiesce(struct xb_softc *sc)
{
    int        mtd;

    // While there are outstanding requests
    while (!TAILQ_EMPTY(&sc->cm_busy)) {
        RING_FINAL_CHECK_FOR_RESPONSES(&sc->ring, mtd);
        if (mtd) {
            /* Recieved request completions, update queue. */
            blkif_int(sc);
        }
        if (!TAILQ_EMPTY(&sc->cm_busy)) {
            /*
             * Still pending requests, wait for the disk i/o
             * to complete.
             */
            HYPERVISOR_yield();
        }
    }
}

/* Kernel dump function for a paravirtualized disk device */
static void
xb_dump_complete(struct xb_command *cm)
{

    xb_enqueue_complete(cm);
}

static int
xb_dump(void *arg, void *vvirtual, vm_offset_t physical, off_t offset,
        size_t length)
{
    struct    disk       *dp = (disk *)arg;
    struct xb_softc    *sc = (struct xb_softc *) dp->d_drv1;
    struct xb_command *cm;
    size_t        chunk;
    int        sbp;
    int        rc = 0;
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);

    if (length <= 0)
        return (rc);

    xb_quiesce(sc);    /* All quiet on the western front. */

    /*
     * If this lock is held, then this module is failing, and a
     * successful kernel dump is highly unlikely anyway.
     */
    mutex_lock(&xsc->xb_io_lock);

    /* Split the 64KB block as needed */
    for (sbp=0; length > 0; sbp++) {
        cm = xb_dequeue_free(sc);
        if (cm == NULL) {
            mutex_unlock(&xsc->xb_io_lock);
            device_printf(sc->xb_dev, "dump: no more commands?\n");
            return (EBUSY);
        }

        if (gnttab_alloc_grant_references(sc->max_request_segments,
                          &cm->gref_head) != 0) {
            xb_free_command(cm);
            mutex_unlock(&xsc->xb_io_lock);
            device_printf(sc->xb_dev, "no more grant allocs?\n");
            return (EBUSY);
        }

        chunk = length > sc->max_request_size
              ? sc->max_request_size : length;
        cm->data = vvirtual;
        cm->datalen = chunk;
        cm->operation = BLKIF_OP_WRITE;
        cm->sector_number = offset / dp->d_sectorsize;
        cm->cm_complete = xb_dump_complete;

        xb_enqueue_ready(cm);

        length -= chunk;
        offset += chunk;
        vvirtual = (char *) vvirtual + chunk;
    }

    /* Tell DOM0 to do the I/O */
    xb_startio(sc);
    mutex_unlock(&xsc->xb_io_lock);

    /* Poll for the completion. */
    xb_quiesce(sc);    /* All quite on the eastern front */

    /* If there were any errors, bail out... */
    while ((cm = xb_dequeue_complete(sc)) != NULL) {
        if (cm->status != BLKIF_RSP_OKAY) {
            device_printf(sc->xb_dev,
                "Dump I/O failed at sector %jd\n",
                cm->sector_number);
            rc = EIO;
        }
        xb_free_command(cm);
    }

    return (rc);
}


static int
blkfront_probe(device_t dev)
{

    if (!strcmp(xenbus_get_type(dev), "vbd")) {
        device_set_desc(dev, "Virtual Block Device");
        device_quiet(dev);
        return (0);
    }

    return (ENXIO);
}

static void
xb_setup_sysctl(struct xb_softc *xb)
{
#if 0
    struct sysctl_ctx_list *sysctl_ctx = NULL;
    struct sysctl_oid      *sysctl_tree = NULL;

    sysctl_ctx = device_get_sysctl_ctx(xb->xb_dev);
    if (sysctl_ctx == NULL)
        return;

    sysctl_tree = device_get_sysctl_tree(xb->xb_dev);
    if (sysctl_tree == NULL)
        return;

    SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
                "max_requests", CTLFLAG_RD, &xb->max_requests, -1,
                "maximum outstanding requests (negotiated)");

    SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
                "max_request_segments", CTLFLAG_RD,
                &xb->max_request_segments, 0,
                "maximum number of pages per requests (negotiated)");

    SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
                "max_request_size", CTLFLAG_RD,
                &xb->max_request_size, 0,
                "maximum size in bytes of a request (negotiated)");

    SYSCTL_ADD_UINT(sysctl_ctx, SYSCTL_CHILDREN(sysctl_tree), OID_AUTO,
                "ring_pages", CTLFLAG_RD,
                &xb->ring_pages, 0,
                "communication channel pages (negotiated)");
#endif
}

static int xs_id_by_name(const char* name)
{
    const char *id_str = strrchr(name, '/');
    id_str = id_str ? id_str + 1 : name;
    return atoi(id_str);
}

/*
 * Setup supplies the backend dir, virtual device.  We place an event
 * channel and shared frame entries.  We watch backend to wait if it's
 * ok.
 */
static int
blkfront_attach(device_t dev)
{
    struct xb_softc *sc;
    const char *name, *node_name;
    uint32_t vdevice;
    int error;
    int i;
    int unit;

    node_name = xenbus_get_node(dev);

    /* FIXME: Use dynamic device id if this is not set. */
    error = xs_scanf(XST_NIL, node_name,
        "virtual-device", NULL, "%" PRIu32, &vdevice);
    if (error) {
        /* On some Xen versions there is no virtual-device property,
         * last part of device name should be used instead
         */
        vdevice = xs_id_by_name(node_name);
    }

    blkfront_vdevice_to_unit(vdevice, &unit, &name);
    if (!strcmp(name, "vblk"))
        device_set_unit(dev, unit);

    sc = (xb_softc *)device_get_softc(dev);
    xb_initq_free(sc);
    xb_initq_busy(sc);
    xb_initq_ready(sc);
    xb_initq_complete(sc);
    xb_initq_bio(sc);
    for (i = 0; i < XBF_MAX_RING_PAGES; i++)
        sc->ring_ref[i] = GRANT_INVALID_REF;

    sc->xb_dev = dev;
    sc->xb_disk = NULL;
    sc->vdevice = vdevice;
    sc->connected = BLKIF_STATE_DISCONNECTED;

    xb_setup_sysctl(sc);

    /* Wait for backend device to publish its protocol capabilities. */
    xenbus_set_state(dev, XenbusStateInitialising);

    return (0);
}

static int
blkfront_suspend(device_t dev)
{
    struct xb_softc *sc = (xb_softc *)device_get_softc(dev);
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);
    int retval;
    int saved_state;

    /* Prevent new requests being issued until we fix things up. */
    mutex_lock(&xsc->xb_io_lock);
    saved_state = sc->connected;
    sc->connected = BLKIF_STATE_SUSPENDED;

    /* Wait for outstanding I/O to drain. */
    retval = 0;
    while (TAILQ_EMPTY(&sc->cm_busy) == 0) {
        if (msleep(&sc->cm_busy, &xsc->xb_io_lock,
               PRIBIO, "blkf_susp", 30 * hz) == EWOULDBLOCK) {
            retval = EBUSY;
            break;
        }
    }
    mutex_unlock(&xsc->xb_io_lock);

    if (retval != 0)
        sc->connected = saved_state;

    return (retval);
}

static int
blkfront_resume(device_t dev)
{
    struct xb_softc *sc = (xb_softc *)device_get_softc(dev);

    DPRINTK("blkfront_resume: %s\n", xenbus_get_node(dev));

    blkif_free(sc);
    blkfront_initialize(sc);
    return (0);
}

static unsigned int
blkfront_read_feature(device_t dev, char *name)
{
    unsigned int res = 0;

    auto err = xs_gather(XST_NIL, xenbus_get_otherend_path(dev),
        name, "%u", &res, NULL);

    return err ? 0 : res;
}

static inline unsigned int
blkfront_check_feature(device_t dev, char *name, int flag)
{
    return blkfront_read_feature(dev, name) ? flag : 0;
}

static int
blkif_claim_gref(grant_ref_t *gref_head,
                 device_t dev,
                 bus_addr_t addr,
                 int readonly)
{
    auto ref = gnttab_claim_grant_reference(gref_head);
    /*
    * GNTTAB_LIST_END == 0xffffffff, but it is private
    * to gnttab.c.
    */
    KASSERT(ref != ~0, ("grant_reference failed"));

    gnttab_grant_foreign_access_ref(
            ref,
            xenbus_get_otherend_id(dev),
            addr >> PAGE_SHIFT,
            readonly);

    return ref;
}

class indirect_page
{
public:
    indirect_page()
        : _va(memory::alloc_page()) {}

    ~indirect_page() { memory::free_page(_va); }

    static constexpr unsigned capacity()
        { return memory::page_size / sizeof(blkif_segment_indirect_t); }

    grant_ref_t alloc_gref(device_t dev);
    void free_gref();

    void set_segment(int seg_num, grant_ref_t gref,
                     u8 first_sect, u8 last_seq);

private:
    void* _va;

    grant_ref_t _gref_list;
    grant_ref_t _gref;
};

class blkfront_indirect_descriptor
{
public:
    blkfront_indirect_descriptor(int max_segs)
        : _pages(pages_required(max_segs))
        , _max_segs(max_segs)
        {}

    static constexpr unsigned total_capacity()
    {
        return indirect_page::capacity()
            * BLKIF_MAX_INDIRECT_PAGES_PER_HEADER_BLOCK;
    }

    void attach(blkif_request_indirect_t *descr)
    {
        _descr = descr;
        _seg_number = 0;
    }

    void configure(uint64_t id,
                   uint8_t operation,
                   blkif_sector_t sector_number,
                   blkif_vdev_t handle);

    void add_segment(grant_ref_t gref, u8 first_sect, u8 last_seq);
    void map(device_t dev);
    void unmap();
    bool has_space() { return _seg_number < _max_segs; }
private:
    static constexpr unsigned pages_required(unsigned max_segs)
    {
        return (max_segs + indirect_page::capacity() - 1) /
            indirect_page::capacity();
    }

    std::vector<indirect_page> _pages;
    int _max_segs;
    int _seg_number = 0;
    blkif_request_indirect_t *_descr = nullptr;
};

class blkfront_indirect_descriptors
{
public:
    blkfront_indirect_descriptors(device_t &dev, uint32_t max_requests);
    ~blkfront_indirect_descriptors();

    blkfront_indirect_descriptor *get()
    {
        auto descr = _descriptors.top();
        _descriptors.pop();
        return descr;
    }
    void put(blkfront_indirect_descriptor *descr)
    {
        _descriptors.push(descr);
    }

    unsigned descriptor_capacity()
    {
        return std::min(_max_segs,
            blkfront_indirect_descriptor::total_capacity());
    }

    bool empty() { return _descriptors.empty(); }
private:
    std::stack<blkfront_indirect_descriptor *> _descriptors;
    unsigned _max_segs;
};

class blkfront_head_descr_base
{
public:
    void attach(blkif_request_t *descr)
    {
      _descr = descr;
      _curr_seg = &descr->seg[0];
    }

    void configure(uint64_t id,
                   uint8_t operation,
                   blkif_sector_t sector_number,
                   blkif_vdev_t handle,
                   uint8_t nr_segments);

    static constexpr unsigned capacity()
        { return BLKIF_MAX_SEGMENTS_PER_HEADER_BLOCK; }

    bool has_space()
    {
        return _curr_seg != &_descr->seg[BLKIF_MAX_SEGMENTS_PER_HEADER_BLOCK];
    }

protected:
    blkif_request_t *_descr = nullptr;
    blkif_request_segment_t *_curr_seg = nullptr;
};

class blkfront_segment_descr_base
{
public:
    void attach(blkif_segment_block_t *descr)
    {
      _descr = descr;
      _curr_seg = &_descr->seg[0];
    }

    bool has_space()
    {
        return _curr_seg != &_descr->seg[BLKIF_MAX_SEGMENTS_PER_SEGMENT_BLOCK];
    }

protected:
    blkif_segment_block_t *_descr = nullptr;
    blkif_request_segment_t *_curr_seg = nullptr;
};

template<typename baseT>
class blkfront_descriptor : public baseT
{
public:
    void add_segment(grant_ref_t gref, u8 first_sect, u8 last_seq)
    {
        assert(baseT::has_space());

        baseT::_curr_seg->gref = gref;
        baseT::_curr_seg->first_sect = first_sect;
        baseT::_curr_seg->last_sect = last_seq;

        baseT::_curr_seg++;
    }
};

typedef blkfront_descriptor<blkfront_head_descr_base> blkfront_head_descr;
typedef blkfront_descriptor<blkfront_segment_descr_base> blkfront_segment_descr;

void indirect_page::set_segment(int seg_num, grant_ref_t gref,
                                u8 first_sect, u8 last_seq)
{
    assert(seg_num < capacity());

    auto seg = static_cast<blkif_segment_indirect_t *>(_va) + seg_num;
    seg->gref = gref;
    seg->first_sect = first_sect;
    seg->last_sect = last_seq;
}

grant_ref_t indirect_page::alloc_gref(device_t dev)
{
    if (gnttab_alloc_grant_references(1, &_gref_list) != 0) {
            device_printf(dev, "No memory for grant references");
            abort();
    }

    auto pa = virt_to_phys(_va);
    _gref = blkif_claim_gref(&_gref_list, dev, pa, 1);
    return _gref;
}

void indirect_page::free_gref()
{
    gnttab_end_foreign_access_references(1, &_gref);
    gnttab_free_grant_references(_gref_list);
}

void blkfront_indirect_descriptor::configure(uint64_t id,
                                             uint8_t operation,
                                             blkif_sector_t sector_number,
                                             blkif_vdev_t handle)
{
    assert(operation == BLKIF_OP_READ ||
           operation == BLKIF_OP_WRITE ||
           operation == BLKIF_OP_WRITE_BARRIER);

    _descr->operation = BLKIF_OP_INDIRECT;
    _descr->indirect_op = operation;
    _descr->id = id;
    _descr->sector_number = sector_number;
    _descr->handle = handle;
}

void blkfront_indirect_descriptor::add_segment(grant_ref_t gref,
                                               u8 first_sect,
                                               u8 last_seq)
{
    assert(has_space());

    auto page_num = _seg_number / indirect_page::capacity();
    auto in_page_num = _seg_number % indirect_page::capacity();
    _pages[page_num].set_segment(in_page_num, gref, first_sect, last_seq);
    _seg_number++;
}

void blkfront_indirect_descriptor::map(device_t dev)
{
    for(auto i = 0; i < pages_required(_seg_number); i++)
    {
        _descr->indirect_grefs[i] = _pages[i].alloc_gref(dev);
    }

    _descr->nr_segments = _seg_number;
}

void blkfront_indirect_descriptor::unmap()
{
    for(auto i = 0; i < pages_required(_seg_number); i++)
    {
        _pages[i].free_gref();
    }
}

blkfront_indirect_descriptors::blkfront_indirect_descriptors(device_t &dev,
    uint32_t max_requests)
{
    _max_segs = blkfront_read_feature(dev, "feature-max-indirect-segments");

    _max_segs = std::min(_max_segs, BLKIF_MAX_INDIRECT_SEGMENTS);

    if (_max_segs != 0) {
        for(auto i = 0; i < max_requests; i++) {
            _descriptors.emplace(new blkfront_indirect_descriptor(_max_segs));
        }
    }
}

blkfront_indirect_descriptors::~blkfront_indirect_descriptors()
{
    while (!_descriptors.empty())
    {
        delete _descriptors.top();
        _descriptors.pop();
    }
}

void blkfront_head_descr_base::configure(uint64_t id,
                                         uint8_t operation,
                                         blkif_sector_t sector_number,
                                         blkif_vdev_t handle,
                                         uint8_t nr_segments)
{
    _descr->id = id;
    _descr->operation = operation;
    _descr->sector_number = sector_number;
    _descr->handle = handle;
    _descr->nr_segments = nr_segments;
}

static void
blkfront_alloc_commands(struct xb_softc* sc)
{
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);

    /* Allocate datastructures based on negotiated values. */
    auto error = bus_dma_tag_create(bus_get_dma_tag(sc->xb_dev),    /* parent */
                                    512, PAGE_SIZE,    /* algnmnt, boundary */
                                    BUS_SPACE_MAXADDR,    /* lowaddr */
                                    BUS_SPACE_MAXADDR,    /* highaddr */
                                    NULL, NULL,        /* filter, filterarg */
                                    sc->max_request_size,
                                    sc->max_request_segments,
                                    PAGE_SIZE,        /* maxsegsize */
                                    BUS_DMA_ALLOCNOW,    /* flags */
                                    busdma_lock_mutex,    /* lockfunc */
                                    &xsc->xb_io_lock,    /* lockarg */
                                    &sc->xb_io_dmat);
    if (error != 0) {
        xenbus_dev_fatal(sc->xb_dev, error,
                 "Cannot allocate parent DMA tag\n");
        return;
    }

    /* Per-transaction data allocation. */
    sc->shadow = (xb_command *)malloc(sizeof(*sc->shadow) * sc->max_requests,
                                      M_XENBLOCKFRONT, M_NOWAIT|M_ZERO);

    for (uint32_t i = 0; i < sc->max_requests; i++) {
        struct xb_command *cm;

        cm = &sc->shadow[i];
        cm->sg_refs = (grant_ref_t *)malloc(sizeof(grant_ref_t)
                   * sc->max_request_segments,
                     M_XENBLOCKFRONT, M_NOWAIT);
        cm->id = i;
        cm->cm_sc = sc;
        if (bus_dmamap_create(sc->xb_io_dmat, 0, &cm->map) != 0)
            break;
        xb_free_command(cm);
    }
}

static void
blkfront_initialize(struct xb_softc *sc)
{
    const char *otherend_path;
    const char *node_path;
    uint32_t max_ring_page_order;
    int error;

    if (xenbus_get_state(sc->xb_dev) != XenbusStateInitialising) {
        /* Initialization has already been performed. */
        return;
    }

    /*
     * Protocol defaults valid even if negotiation for a
     * setting fails.
     */
    max_ring_page_order = 0;
    sc->ring_pages = 1;
    sc->max_request_segments = BLKIF_MAX_SEGMENTS_PER_HEADER_BLOCK;
    sc->max_request_size = XBF_SEGS_TO_SIZE(sc->max_request_segments);
    sc->max_request_blocks = BLKIF_SEGS_TO_BLOCKS(sc->max_request_segments);

    /*
     * Protocol negotiation.
     *
     * \note xs_gather() returns on the first encountered error, so
     *       we must use independant calls in order to guarantee
     *       we don't miss information in a sparsly populated back-end
     *       tree.
     *
     * \note xs_scanf() does not update variables for unmatched
     *     fields.
     */
    otherend_path = xenbus_get_otherend_path(sc->xb_dev);
    node_path = xenbus_get_node(sc->xb_dev);

    /* Support both backend schemes for relaying ring page limits. */
    (void)xs_scanf(XST_NIL, otherend_path,
               "max-ring-page-order", NULL, "%" PRIu32,
               &max_ring_page_order);
    sc->ring_pages = 1 << max_ring_page_order;
    (void)xs_scanf(XST_NIL, otherend_path,
               "max-ring-pages", NULL, "%" PRIu32,
               &sc->ring_pages);
    if (sc->ring_pages < 1)
        sc->ring_pages = 1;

    sc->max_requests = BLKIF_MAX_RING_REQUESTS(sc->ring_pages * PAGE_SIZE);
    (void)xs_scanf(XST_NIL, otherend_path,
               "max-requests", NULL, "%" PRIu32,
               &sc->max_requests);

    (void)xs_scanf(XST_NIL, otherend_path,
               "max-request-segments", NULL, "%" PRIu32,
               &sc->max_request_segments);

    (void)xs_scanf(XST_NIL, otherend_path,
               "max-request-size", NULL, "%" PRIu32,
               &sc->max_request_size);

    if (sc->ring_pages > XBF_MAX_RING_PAGES) {
        device_printf(sc->xb_dev, "Back-end specified ring-pages of "
                  "%u limited to front-end limit of %zu.\n",
                  sc->ring_pages, XBF_MAX_RING_PAGES);
        sc->ring_pages = XBF_MAX_RING_PAGES;
    }

    if (powerof2(sc->ring_pages) == 0) {
        uint32_t new_page_limit;

        new_page_limit = 0x01 << (fls(sc->ring_pages) - 1);
        device_printf(sc->xb_dev, "Back-end specified ring-pages of "
                  "%u is not a power of 2. Limited to %u.\n",
                  sc->ring_pages, new_page_limit);
        sc->ring_pages = new_page_limit;
    }

    if (sc->max_requests > XBF_MAX_REQUESTS) {
        device_printf(sc->xb_dev, "Back-end specified max_requests of "
                  "%u limited to front-end limit of %u.\n",
                  sc->max_requests, XBF_MAX_REQUESTS);
        sc->max_requests = XBF_MAX_REQUESTS;
    }

    if (sc->max_request_segments > XBF_MAX_SEGMENTS_PER_REQUEST) {
        device_printf(sc->xb_dev, "Back-end specified "
                  "max_request_segments of %u limited to "
                  "front-end limit of %u.\n",
                  sc->max_request_segments,
                  XBF_MAX_SEGMENTS_PER_REQUEST);
        sc->max_request_segments = XBF_MAX_SEGMENTS_PER_REQUEST;
    }

    if (sc->max_request_size > XBF_MAX_REQUEST_SIZE) {
        device_printf(sc->xb_dev, "Back-end specified "
                  "max_request_size of %u limited to front-end "
                  "limit of %u.\n", sc->max_request_size,
                  XBF_MAX_REQUEST_SIZE);
        sc->max_request_size = XBF_MAX_REQUEST_SIZE;
    }

     if (sc->max_request_size > XBF_SEGS_TO_SIZE(sc->max_request_segments)) {
         device_printf(sc->xb_dev, "Back-end specified "
                   "max_request_size of %u limited to front-end "
                   "limit of %u.  (Too few segments.)\n",
                   sc->max_request_size,
                   XBF_SEGS_TO_SIZE(sc->max_request_segments));
         sc->max_request_size =
             XBF_SEGS_TO_SIZE(sc->max_request_segments);
     }

    sc->max_request_blocks = BLKIF_SEGS_TO_BLOCKS(sc->max_request_segments);

    if (setup_blkring(sc) != 0)
        return;

    /* Support both backend schemes for relaying ring page limits. */
    if (sc->ring_pages > 1) {
        error = xs_printf(XST_NIL, node_path,
                 "num-ring-pages","%u", sc->ring_pages);
        if (error) {
            xenbus_dev_fatal(sc->xb_dev, error,
                     "writing %s/num-ring-pages",
                     node_path);
            return;
        }

        error = xs_printf(XST_NIL, node_path,
                 "ring-page-order", "%u",
                 fls(sc->ring_pages) - 1);
        if (error) {
            xenbus_dev_fatal(sc->xb_dev, error,
                     "writing %s/ring-page-order",
                     node_path);
            return;
        }
    }

    error = xs_printf(XST_NIL, node_path,
             "max-requests","%u", sc->max_requests);
    if (error) {
        xenbus_dev_fatal(sc->xb_dev, error,
                 "writing %s/max-requests",
                 node_path);
        return;
    }

    error = xs_printf(XST_NIL, node_path,
             "max-request-segments","%u", sc->max_request_segments);
    if (error) {
        xenbus_dev_fatal(sc->xb_dev, error,
                 "writing %s/max-request-segments",
                 node_path);
        return;
    }

    error = xs_printf(XST_NIL, node_path,
             "max-request-size","%u", sc->max_request_size);
    if (error) {
        xenbus_dev_fatal(sc->xb_dev, error,
                 "writing %s/max-request-size",
                 node_path);
        return;
    }

    error = xs_printf(XST_NIL, node_path, "event-channel",
              "%u", irq_to_evtchn_port(sc->irq));
    if (error) {
        xenbus_dev_fatal(sc->xb_dev, error,
                 "writing %s/event-channel",
                 node_path);
        return;
    }

    error = xs_printf(XST_NIL, node_path,
              "protocol", "%s", XEN_IO_PROTO_ABI_NATIVE);
    if (error) {
        xenbus_dev_fatal(sc->xb_dev, error,
                 "writing %s/protocol",
                 node_path);
        return;
    }

    xenbus_set_state(sc->xb_dev, XenbusStateInitialised);
}

static int
setup_blkring(struct xb_softc *sc)
{
    blkif_sring_t *sring;
    uintptr_t sring_page_addr;
    int error;
    int i;

    sring = (blkif_sring_t *)memory::alloc_phys_contiguous_aligned(
            sc->ring_pages * PAGE_SIZE, PAGE_SIZE);
    memset(sring, 0, sc->ring_pages * PAGE_SIZE);
    if (sring == NULL) {
        xenbus_dev_fatal(sc->xb_dev, ENOMEM, "allocating shared ring");
        return (ENOMEM);
    }
    SHARED_RING_INIT(sring);
    FRONT_RING_INIT(&sc->ring, sring, sc->ring_pages * PAGE_SIZE);

    for (i = 0, sring_page_addr = (uintptr_t)sring;
         i < sc->ring_pages;
         i++, sring_page_addr += PAGE_SIZE) {

        error = xenbus_grant_ring(sc->xb_dev,
            (vtomach(sring_page_addr) >> PAGE_SHIFT), &sc->ring_ref[i]);
        if (error) {
            xenbus_dev_fatal(sc->xb_dev, error,
                     "granting ring_ref(%d)", i);
            return (error);
        }
    }
    if (sc->ring_pages == 1) {
        error = xs_printf(XST_NIL, xenbus_get_node(sc->xb_dev),
                  "ring-ref", "%u", sc->ring_ref[0]);
        if (error) {
            xenbus_dev_fatal(sc->xb_dev, error,
                     "writing %s/ring-ref",
                     xenbus_get_node(sc->xb_dev));
            return (error);
        }
    } else {
        for (i = 0; i < sc->ring_pages; i++) {
            char ring_ref_name[]= "ring_refXX";

            snprintf(ring_ref_name, sizeof(ring_ref_name),
                 "ring-ref%u", i);
            error = xs_printf(XST_NIL, xenbus_get_node(sc->xb_dev),
                     ring_ref_name, "%u", sc->ring_ref[i]);
            if (error) {
                xenbus_dev_fatal(sc->xb_dev, error,
                         "writing %s/%s",
                         xenbus_get_node(sc->xb_dev),
                         ring_ref_name);
                return (error);
            }
        }
    }

    error = bind_listening_port_to_irqhandler(
        xenbus_get_otherend_id(sc->xb_dev),
        "xbd", (driver_intr_t)blkif_int, sc,
        INTR_TYPE_BIO | INTR_MPSAFE, &sc->irq);
    if (error) {
        xenbus_dev_fatal(sc->xb_dev, error,
            "bind_evtchn_to_irqhandler failed");
        return (error);
    }

    return (0);
}

/**
 * Callback received when the backend's state changes.
 */
static void
blkfront_backend_changed(device_t dev, XenbusState backend_state)
{
    struct xb_softc *sc = (xb_softc *)device_get_softc(dev);

    DPRINTK("backend_state=%d\n", backend_state);

    switch (backend_state) {
    case XenbusStateUnknown:
    case XenbusStateInitialising:
    case XenbusStateReconfigured:
    case XenbusStateReconfiguring:
    case XenbusStateClosed:
        break;

    case XenbusStateInitWait:
    case XenbusStateInitialised:
        blkfront_initialize(sc);
        break;

    case XenbusStateConnected:
        blkfront_initialize(sc);
        blkfront_connect(sc);
        break;

    case XenbusStateClosing:
        if (sc->users > 0)
            xenbus_dev_error(dev, -EBUSY,
                     "Device in use; refusing to close");
        else
            blkfront_closing(dev);
        break;
    }
}

/*
** Invoked when the backend is finally 'ready' (and has published
** the details about the physical device - #sectors, size, etc).
*/
static void
blkfront_connect(struct xb_softc *sc)
{
    device_t dev = sc->xb_dev;
    unsigned long sectors, sector_size;
    unsigned int binfo;
    int err;
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);

    if( (sc->connected == BLKIF_STATE_CONNECTED) ||
        (sc->connected == BLKIF_STATE_SUSPENDED) )
        return;

    DPRINTK("blkfront.c:connect:%s.\n", xenbus_get_otherend_path(dev));

    err = xs_gather(XST_NIL, xenbus_get_otherend_path(dev),
            "sectors", "%lu", &sectors,
            "info", "%u", &binfo,
            "sector-size", "%lu", &sector_size,
            NULL);
    if (err) {
        xenbus_dev_fatal(dev, err,
            "reading backend fields at %s",
            xenbus_get_otherend_path(dev));
        return;
    }

    sc->xb_flags |= blkfront_check_feature(dev, "feature-barrier", XB_BARRIER);
    sc->xb_flags |= blkfront_check_feature(dev, "feature-flush-cache", XB_FLUSH);

    sc->indirect_descriptors =
        new blkfront_indirect_descriptors(sc->xb_dev, sc->max_requests);

    if (!sc->indirect_descriptors->empty()) {
        sc->max_request_segments =
            sc->indirect_descriptors->descriptor_capacity();
        sc->max_request_blocks = 1;
        sc->max_request_size = XBF_SEGS_TO_SIZE(sc->max_request_segments);
    }

    blkfront_alloc_commands(sc);

    if (sc->xb_disk == NULL) {
        device_printf(dev, "%juMB <%s> at %s",
            (uintmax_t) sectors / (1048576 / sector_size),
            device_get_desc(dev),
            xenbus_get_node(dev));
        bus_print_child_footer(device_get_parent(dev), dev);

        xlvbd_add(sc, sectors, sc->vdevice, binfo, sector_size);
    }

    mutex_lock(&xsc->xb_io_lock);
    sc->connected = BLKIF_STATE_CONNECTED;
    sc->xb_flags |= XB_READY;
    mutex_unlock(&xsc->xb_io_lock);

    (void)xenbus_set_state(dev, XenbusStateConnected);

    /* Kick pending requests. */
    mutex_lock(&xsc->xb_io_lock);
    xb_startio(sc);
    mutex_unlock(&xsc->xb_io_lock);
}

/**
 * Handle the change of state of the backend to Closing.  We must delete our
 * device-layer structures now, to ensure that writes are flushed through to
 * the backend.  Once this is done, we can switch to Closed in
 * acknowledgement.
 */
static void
blkfront_closing(device_t dev)
{
    struct xb_softc *sc = (xb_softc *)device_get_softc(dev);

    xenbus_set_state(dev, XenbusStateClosing);

    DPRINTK("blkfront_closing: %s removed\n", xenbus_get_node(dev));

    if (sc->xb_disk != NULL) {
        disk_destroy(sc->xb_disk);
        sc->xb_disk = NULL;
    }

    xenbus_set_state(dev, XenbusStateClosed);
}


static int
blkfront_detach(device_t dev)
{
    struct xb_softc *sc = (xb_softc *)device_get_softc(dev);

    DPRINTK("blkfront_remove: %s removed\n", xenbus_get_node(dev));

    blkif_free(sc);
    return 0;
}

static void
xb_freeze(struct xb_softc *sc, unsigned xb_flag)
{
    if (xb_flag != XB_NONE && (sc->xb_flags & xb_flag) != 0)
        return;

    sc->xb_flags |= xb_flag;
    sc->xb_qfrozen_cnt++;
}

static void
xb_thaw(struct xb_softc *sc, unsigned xb_flag)
{
    if (xb_flag != XB_NONE && (sc->xb_flags & xb_flag) == 0)
        return;

    sc->xb_flags &= ~xb_flag;
    sc->xb_qfrozen_cnt--;
}

static void
xb_cm_freeze(struct xb_softc *sc, struct xb_command *cm, unsigned cm_flag)
{
    if ((cm->cm_flags & XB_CMD_FROZEN) != 0)
        return;

    cm->cm_flags |= XB_CMD_FROZEN | cm_flag;
    xb_freeze(sc, XB_NONE);
}

static void
xb_cm_thaw(struct xb_softc *sc, struct xb_command *cm)
{
    if ((cm->cm_flags & XB_CMD_FROZEN) == 0)
        return;

    cm->cm_flags &= ~XB_CMD_FROZEN;
    xb_thaw(sc, XB_NONE);
}

static inline void
flush_requests(struct xb_softc *sc)
{
    int notify;

    RING_PUSH_REQUESTS_AND_CHECK_NOTIFY(&sc->ring, notify);

    if (notify)
        notify_remote_via_irq(sc->irq);
}

static void
blkif_restart_queue_callback(void *arg)
{
    struct xb_softc *sc = (xb_softc *)arg;
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);

    mutex_lock(&xsc->xb_io_lock);

    xb_startio(sc);

    mutex_unlock(&xsc->xb_io_lock);
}

static int
blkif_open(struct disk *dp)
{
    struct xb_softc    *sc = (struct xb_softc *)dp->d_drv1;

    if (sc == NULL) {
        printf("xb%d: not found", sc->xb_unit);
        return (ENXIO);
    }

    sc->xb_flags |= XB_OPEN;
    sc->users++;
    return (0);
}

static int
blkif_close(struct disk *dp)
{
    struct xb_softc    *sc = (struct xb_softc *)dp->d_drv1;

    if (sc == NULL)
        return (ENXIO);
    sc->xb_flags &= ~XB_OPEN;
    if (--(sc->users) == 0) {
        /*
         * Check whether we have been instructed to close.  We will
         * have ignored this request initially, as the device was
         * still mounted.
         */
        if (xenbus_get_otherend_state(sc->xb_dev) == XenbusStateClosing)
            blkfront_closing(sc->xb_dev);
    }
    return (0);
}

static int
blkif_ioctl(struct disk *dp, u_long cmd, void *addr, int flag, struct thread *td)
{
    struct xb_softc    *sc = (struct xb_softc *)dp->d_drv1;

    if (sc == NULL)
        return (ENXIO);

    return (ENOTTY);
}

static void
xb_free_command(struct xb_command *cm)
{

    KASSERT((cm->cm_flags & XB_ON_XBQ_MASK) == 0,
        ("Freeing command that is still on a queue\n"));

    cm->cm_flags = 0;
    cm->bp = NULL;
    cm->cm_complete = NULL;
    xb_enqueue_free(cm);
}

/*
 * blkif_queue_request
 *
 * request block io
 *
 * id: for guest use only.
 * operation: BLKIF_OP_{READ,WRITE,PROBE}
 * buffer: buffer to read/write into. this should be a
 *   virtual address in the guest os.
 */
static struct xb_command *
xb_bio_command(struct xb_softc *sc)
{
    struct xb_command *cm;
    struct bio *bp;

    if (unlikely(sc->connected != BLKIF_STATE_CONNECTED))
        return (NULL);

    bp = xb_dequeue_bio(sc);
    if (bp == NULL)
        return (NULL);

    if ((cm = xb_dequeue_free(sc)) == NULL) {
        xb_requeue_bio(sc, bp);
        return (NULL);
    }

    if (gnttab_alloc_grant_references(sc->max_request_segments,
        &cm->gref_head) != 0) {
        gnttab_request_free_callback(&sc->callback,
            blkif_restart_queue_callback, sc,
            sc->max_request_segments);
        xb_requeue_bio(sc, bp);
        xb_enqueue_free(cm);
        sc->xb_flags |= XB_FROZEN;
        return (NULL);
    }

    cm->bp = bp;
    cm->data = bp->bio_data;
    cm->datalen = bp->bio_bcount;
    switch (bp->bio_cmd) {
        case BIO_READ:
            cm->operation = BLKIF_OP_READ;
            break;
        case BIO_WRITE:
            cm->operation = BLKIF_OP_WRITE;
            if ((bp->bio_flags & BIO_ORDERED) != 0) {
                if ((sc->xb_flags & XB_BARRIER) != 0) {
                    cm->operation = BLKIF_OP_WRITE_BARRIER;
                } else {
                    /*
                     * Single step this command.
                     */
                    cm->cm_flags |= XB_CMD_FREEZE;
                    if (!TAILQ_EMPTY(&sc->cm_busy)) {
                        /*
                         * Wait for in-flight requests to
                         * finish.
                         */
                        xb_freeze(sc, XB_WAIT_IDLE);
                        xb_requeue_ready(cm);
                        return nullptr;
                    }
                }
            }
            break;
        case BIO_FLUSH:
            if ((sc->xb_flags & XB_FLUSH) != 0)
                cm->operation = BLKIF_OP_FLUSH_DISKCACHE;
            else if ((sc->xb_flags & XB_BARRIER) != 0)
                cm->operation = BLKIF_OP_WRITE_BARRIER;
            else
                /* Should have already been handled by xb_quiesce */
                panic("flush request, but no flush support available");
            break;
        default:
            panic("Unrecognized bio request");
            break;
    }
    cm->sector_number = (blkif_sector_t)bp->bio_offset / sc->xb_disk->d_sectorsize;

    return (cm);
}

static int
blkif_queue_request(struct xb_softc *sc, struct xb_command *cm)
{
    int    error;

    error = bus_dmamap_load(sc->xb_io_dmat, cm->map, cm->data, cm->datalen,
        blkif_queue_cb, cm, 0);
    if (error == EINPROGRESS) {
        xb_cm_freeze(sc, cm, XB_NONE);
        return (0);
    }

    return (error);
}

static void
blkif_claim_data_grefs(device_t dev, struct xb_command *cm,
                       bus_dma_segment_t *segs, int nsegs)
{
    for (auto i = 0; i < nsegs; i++) {
        cm->sg_refs[i] = blkif_claim_gref(&cm->gref_head, dev, segs[i].ds_addr,
            cm->operation == BLKIF_OP_WRITE);
    }
}

template<typename DescrT>
static void
blkif_put_segments(DescrT &descr,
                   bus_dma_segment_t *&segs, int &nsegs,
                   grant_ref_t *&sg_ref)
{
    while (descr.has_space() && nsegs != 0) {
        auto buffer_ma = segs->ds_addr;
        uint8_t fsect = (buffer_ma & PAGE_MASK) >> XBD_SECTOR_SHFT;
        uint8_t lsect = fsect + (segs->ds_len  >> XBD_SECTOR_SHFT) - 1;

        KASSERT(lsect <= 7, ("XEN disk driver data cannot "
            "cross a page boundary"));

        descr.add_segment(*sg_ref, fsect, lsect);

        sg_ref++;
        segs++;
        nsegs--;
    }
}

static void
blkif_put_to_ring_inplace(struct xb_softc *sc, xb_command *cm,
                          bus_dma_segment_t *&segs, int &nsegs,
                          blkif_vdev_t dev_handle, grant_ref_t *&sg_ref)
{
    /* Fill out a communications ring structure. */
    auto ring_req = RING_GET_REQUEST(&sc->ring, sc->ring.req_prod_pvt);

    blkfront_head_descr descr;
    descr.attach(ring_req);
    descr.configure(cm->id,
                    cm->operation,
                    cm->sector_number,
                    dev_handle,
                    nsegs);

    blkif_put_segments(descr, segs, nsegs, sg_ref);

    sc->ring.req_prod_pvt++;

    while (nsegs != 0) {
        ring_req = RING_GET_REQUEST(&sc->ring, sc->ring.req_prod_pvt);
        blkfront_segment_descr descr;
        descr.attach(reinterpret_cast<blkif_segment_block_t*>(ring_req));

        blkif_put_segments(descr, segs, nsegs, sg_ref);

        sc->ring.req_prod_pvt++;
    }
}

static blkfront_indirect_descriptor*
blkif_put_to_ring_indirect(struct xb_softc *sc, xb_command *cm,
                           bus_dma_segment_t *&segs, int &nsegs,
                           blkif_vdev_t dev_handle, grant_ref_t *&sg_ref)
{
    assert(nsegs <= sc->indirect_descriptors->descriptor_capacity());

    /* Fill out a communications ring structure. */
    auto ring_req = RING_GET_REQUEST(&sc->ring, sc->ring.req_prod_pvt);

    auto descr = sc->indirect_descriptors->get();
    assert(descr != nullptr);
    descr->attach(reinterpret_cast<blkif_request_indirect_t*>(ring_req));
    descr->configure(cm->id,
                     cm->operation,
                     cm->sector_number,
                     dev_handle);
    blkif_put_segments(*descr, segs, nsegs, sg_ref);
    descr->map(sc->xb_dev);

    sc->ring.req_prod_pvt++;

    return descr;
}

static inline bool
blkif_use_indirect_descr(struct xb_softc *sc, int nsegs)
{
    return (nsegs > blkfront_head_descr::capacity() &&
            !sc->indirect_descriptors->empty());
}

static void
blkif_put_to_ring(struct xb_softc *sc, xb_command *cm,
                  bus_dma_segment_t *segs, int nsegs,
                  blkif_vdev_t dev_handle, grant_ref_t *sg_ref)
{
    if (blkif_use_indirect_descr(sc, nsegs)) {
        cm->ind_descr = blkif_put_to_ring_indirect(sc, cm, segs, nsegs,
                                                   dev_handle, sg_ref);
    } else {
        blkif_put_to_ring_inplace(sc, cm, segs, nsegs, dev_handle, sg_ref);
        cm->ind_descr = nullptr;
    }
}

static void
blkif_queue_cb(void *arg, bus_dma_segment_t *segs, int nsegs, int error)
{
    struct xb_softc *sc;
    struct xb_command *cm;
    int op;

    cm = (xb_command *)arg;
    sc = cm->cm_sc;

//printf("%s: Start\n", __func__);
    if (error) {
        printf("error %d in blkif_queue_cb\n", error);
        cm->bp->bio_error = EIO;
        biodone(cm->bp, false);
        xb_free_command(cm);
        return;
    }

    cm->nseg = nsegs;
    blkif_claim_data_grefs(sc->xb_dev, cm, segs, nsegs);

    blkif_put_to_ring(sc, cm, segs, nsegs,
                      (blkif_vdev_t)(uintptr_t)sc->xb_disk,
                      cm->sg_refs);

    if (cm->operation == BLKIF_OP_READ)
        op = BUS_DMASYNC_PREREAD;
    else if (cm->operation == BLKIF_OP_WRITE)
        op = BUS_DMASYNC_PREWRITE;
    else
        op = 0;
    bus_dmamap_sync(sc->xb_io_dmat, cm->map, op);

    gnttab_free_grant_references(cm->gref_head);

    xb_enqueue_busy(cm);

    /*
     * This flag means that we're probably executing in the busdma swi
     * instead of in the startio context, so an explicit flush is needed.
     */
    if (cm->cm_flags & XB_CMD_FROZEN)
        flush_requests(sc);

//printf("%s: Done\n", __func__);
    return;
}

/*
 * Dequeue buffers and place them in the shared communication ring.
 * Return when no more requests can be accepted or all buffers have
 * been queued.
 *
 * Signal XEN once the ring has been filled out.
 */
static void
xb_startio(struct xb_softc *sc)
{
    struct xb_command *cm;
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);
    int error, queued = 0;

    assert(mutex_owned(&xsc->xb_io_lock));

    if (sc->connected != BLKIF_STATE_CONNECTED)
        return;

    while (RING_FREE_REQUESTS(&sc->ring) >= sc->max_request_blocks) {
        if (sc->xb_qfrozen_cnt != 0) {
            break;
        }

        cm = xb_dequeue_ready(sc);

        if (cm == NULL)
            cm = xb_bio_command(sc);

        if (cm == NULL)
            break;

        if ((cm->cm_flags & XB_CMD_FREEZE) != 0) {
            /*
             * Single step command.  Future work is
             * held off until this command completes.
             */
            xb_cm_freeze(sc, cm, XB_CMD_FREEZE);
        }

        if ((error = blkif_queue_request(sc, cm)) != 0) {
            printf("blkif_queue_request returned %d\n", error);
            break;
        }
        queued++;
    }

    if (queued != 0)
        flush_requests(sc);
}

static void
blkif_int(void *_xsc)
{
    bf_softc *xsc = reinterpret_cast<bf_softc *>(_xsc);
    struct xb_softc *sc = (xb_softc *)xsc;
    struct xb_command *cm;
    blkif_response_t *bret;
    RING_IDX i, rp;
    int op;

    mutex_lock(&xsc->xb_io_lock);

    if (unlikely(sc->connected == BLKIF_STATE_DISCONNECTED)) {
        mutex_unlock(&xsc->xb_io_lock);
        return;
    }

 again:
    rp = sc->ring.sring->rsp_prod;
    rmb(); /* Ensure we see queued responses up to 'rp'. */

    for (i = sc->ring.rsp_cons; i != rp;) {
        bret = RING_GET_RESPONSE(&sc->ring, i);
        cm   = &sc->shadow[bret->id];

        xb_remove_busy(cm);
        i += blkif_completion(cm);

        if (cm->operation == BLKIF_OP_READ)
            op = BUS_DMASYNC_POSTREAD;
        else if ((cm->operation == BLKIF_OP_WRITE) ||
            (cm->operation == BLKIF_OP_WRITE_BARRIER))
            op = BUS_DMASYNC_POSTWRITE;
        else
            op = 0;
        bus_dmamap_sync(sc->xb_io_dmat, cm->map, op);
        bus_dmamap_unload(sc->xb_io_dmat, cm->map);

        /*
         * If commands are completing then resources are probably
         * being freed as well.  It's a cheap assumption even when
         * wrong.
         */
        xb_cm_thaw(sc, cm);

        /*
         * Directly call the i/o complete routine to save an
         * an indirection in the common case.
         */
        cm->status = bret->status;
        if (cm->bp)
            xb_bio_complete(sc, cm);
        else if (cm->cm_complete)
            (cm->cm_complete)(cm);
        else
            xb_free_command(cm);
    }

    sc->ring.rsp_cons = i;

    if (i != sc->ring.req_prod_pvt) {
        int more_to_do;
        RING_FINAL_CHECK_FOR_RESPONSES(&sc->ring, more_to_do);
        if (more_to_do)
            goto again;
    } else {
        sc->ring.sring->rsp_event = i + 1;
    }

    if (TAILQ_EMPTY(&sc->cm_busy)) {
        xb_thaw(sc, XB_WAIT_IDLE);
    }

    xb_startio(sc);

    if (unlikely(sc->connected == BLKIF_STATE_SUSPENDED))
        wakeup(&sc->cm_busy);

    xsc->_bio_queue_waiters.wake_all();
    mutex_unlock(&xsc->xb_io_lock);
}

static void
blkif_free(struct xb_softc *sc)
{
    uint8_t *sring_page_ptr;
    bf_softc *xsc = reinterpret_cast<bf_softc *>(sc);
    int i;

    /* Prevent new requests being issued until we fix things up. */
    mutex_lock(&xsc->xb_io_lock);
    sc->connected = BLKIF_STATE_DISCONNECTED;
    mutex_unlock(&xsc->xb_io_lock);

    /* Free resources associated with old device channel. */
    if (sc->ring.sring != NULL) {
        sring_page_ptr = (uint8_t *)sc->ring.sring;
        for (i = 0; i < sc->ring_pages; i++) {
            if (sc->ring_ref[i] != GRANT_INVALID_REF) {
                gnttab_end_foreign_access_ref(sc->ring_ref[i]);
                sc->ring_ref[i] = GRANT_INVALID_REF;
            }
            sring_page_ptr += PAGE_SIZE;
        }
        memory::free_phys_contiguous_aligned(sc->ring.sring);
        sc->ring.sring = NULL;
    }

    if (sc->shadow) {

        for (i = 0; i < sc->max_requests; i++) {
            struct xb_command *cm;

            cm = &sc->shadow[i];
            if (cm->sg_refs != NULL) {
                free(cm->sg_refs, M_XENBLOCKFRONT);
                cm->sg_refs = NULL;
            }

            bus_dmamap_destroy(sc->xb_io_dmat, cm->map);
        }
        free(sc->shadow, M_XENBLOCKFRONT);
        sc->shadow = NULL;

        bus_dma_tag_destroy(sc->xb_io_dmat);

        xb_initq_free(sc);
        xb_initq_ready(sc);
        xb_initq_complete(sc);
    }

    if (sc->irq) {
        unbind_from_irqhandler(sc->irq);
        sc->irq = 0;
    }

    delete sc->indirect_descriptors;
}

static int
blkif_completion(struct xb_command *s)
{
//printf("%s: Req %p(%d)\n", __func__, s, s->nseg);
    gnttab_end_foreign_access_references(s->nseg, s->sg_refs);
    if (s->ind_descr) {
        s->ind_descr->unmap();
        s->cm_sc->indirect_descriptors->put(s->ind_descr);
        return 1;
    }
    return (BLKIF_SEGS_TO_BLOCKS(s->nseg));
}

struct device *blkfront_from_softc(struct xb_softc *s)
{
    return s->xb_dev;
}

/* ** Driver registration ** */
static device_method_t blkfront_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,         blkfront_probe),
    DEVMETHOD(device_attach,        blkfront_attach),
    DEVMETHOD(device_detach,        blkfront_detach),
    DEVMETHOD(device_shutdown,      bus_generic_shutdown),
    DEVMETHOD(device_suspend,       blkfront_suspend),
    DEVMETHOD(device_resume,        blkfront_resume),

    /* Xenbus interface */
    DEVMETHOD(xenbus_otherend_changed, blkfront_backend_changed),

    { 0, 0 }
};

driver_t blkfront_driver = {
    "xbd",
    blkfront_methods,
    sizeof(struct xb_softc),
};
