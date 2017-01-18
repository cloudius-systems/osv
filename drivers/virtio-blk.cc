/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <sys/cdefs.h>

#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>

#include <osv/mempool.hh>
#include <osv/mmu.hh>

#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include <osv/sched.hh>
#include "osv/trace.hh"

#include <osv/device.h>
#include <osv/bio.h>

TRACEPOINT(trace_virtio_blk_read_config_capacity, "capacity=%lu", u64);
TRACEPOINT(trace_virtio_blk_read_config_size_max, "size_max=%u", u32);
TRACEPOINT(trace_virtio_blk_read_config_seg_max, "seg_max=%u", u32);
TRACEPOINT(trace_virtio_blk_read_config_geometry, "cylinders=%u, heads=%u, sectors=%u", u32, u32, u32);
TRACEPOINT(trace_virtio_blk_read_config_blk_size, "blk_size=%u", u32);
TRACEPOINT(trace_virtio_blk_read_config_topology, "physical_block_exp=%u, alignment_offset=%u, min_io_size=%u, opt_io_size=%u", u32, u32, u32, u32);
TRACEPOINT(trace_virtio_blk_read_config_wce, "wce=%u", u32);
TRACEPOINT(trace_virtio_blk_read_config_ro, "readonly=true");
TRACEPOINT(trace_virtio_blk_make_request_seg_max, "request of size %d needs more segment than the max %d", size_t, u32);
TRACEPOINT(trace_virtio_blk_make_request_readonly, "write on readonly device");
TRACEPOINT(trace_virtio_blk_wake, "");
TRACEPOINT(trace_virtio_blk_strategy, "bio=%p", struct bio*);
TRACEPOINT(trace_virtio_blk_req_ok, "bio=%p, sector=%lu, len=%lu, type=%x", struct bio*, u64, size_t, u32);
TRACEPOINT(trace_virtio_blk_req_unsupp, "bio=%p, sector=%lu, len=%lu, type=%x", struct bio*, u64, size_t, u32);
TRACEPOINT(trace_virtio_blk_req_err, "bio=%p, sector=%lu, len=%lu, type=%x", struct bio*, u64, size_t, u32);

using namespace memory;


namespace virtio {

int blk::_instance = 0;


struct blk_priv {
    blk* drv;
};

static void
blk_strategy(struct bio *bio)
{
    struct blk_priv *prv = reinterpret_cast<struct blk_priv*>(bio->bio_dev->private_data);

    trace_virtio_blk_strategy(bio);
    bio->bio_offset += bio->bio_dev->offset;
    prv->drv->make_request(bio);
}

static int
blk_read(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_read(dev, uio, ioflags);
}

static int
blk_write(struct device *dev, struct uio *uio, int ioflags)
{
    auto* prv = reinterpret_cast<struct blk_priv*>(dev->private_data);

    if (prv->drv->is_readonly()) return EROFS;

    return bdev_write(dev, uio, ioflags);
}

static struct devops blk_devops {
    no_open,
    no_close,
    blk_read,
    blk_write,
    no_ioctl,
    no_devctl,
    blk_strategy,
};

struct driver blk_driver = {
    "virtio_blk",
    &blk_devops,
    sizeof(struct blk_priv),
};

bool blk::ack_irq()
{
    auto isr = virtio_conf_readb(VIRTIO_PCI_ISR);
    auto queue = get_virt_queue(0);

    if (isr) {
        queue->disable_interrupts();
        return true;
    } else {
        return false;
    }

}

blk::blk(pci::device& pci_dev)
    : virtio_driver(pci_dev), _ro(false)
{

    _driver_name = "virtio-blk";
    _id = _instance++;
    virtio_i("VIRTIO BLK INSTANCE %d", _id);

    setup_features();
    read_config();

    //register the single irq callback for the block
    sched::thread* t = sched::thread::make([this] { this->req_done(); },
            sched::thread::attr().name("virtio-blk"));
    t->start();
    auto queue = get_virt_queue(0);
    if (pci_dev.is_msix()) {
        _msi.easy_register({ { 0, [=] { queue->disable_interrupts(); }, t } });
    } else {
        _irq.reset(new pci_interrupt(pci_dev,
                                     [=] { return ack_irq(); },
                                     [=] { t->wake(); }));
    }

    // Enable indirect descriptor
    queue->set_use_indirect(true);

    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    struct blk_priv* prv;
    struct device *dev;
    std::string dev_name("vblk");
    dev_name += std::to_string(_disk_idx++);

    dev = device_create(&blk_driver, dev_name.c_str(), D_BLK);
    prv = reinterpret_cast<struct blk_priv*>(dev->private_data);
    prv->drv = this;
    dev->size = prv->drv->size();
    read_partition_table(dev);

    debugf("virtio-blk: Add blk device instances %d as %s, devsize=%lld\n", _id, dev_name.c_str(), dev->size);
}

blk::~blk()
{
    //TODO: In theory maintain the list of free instances and gc it
    // including the thread objects and their stack
}

void blk::read_config()
{
    //read all of the block config (including size, mce, topology,..) in one shot
    virtio_conf_read(virtio_pci_config_offset(), &_config, sizeof(_config));

    trace_virtio_blk_read_config_capacity(_config.capacity);

    if (get_guest_feature_bit(VIRTIO_BLK_F_SIZE_MAX))
        trace_virtio_blk_read_config_size_max(_config.size_max);
    if (get_guest_feature_bit(VIRTIO_BLK_F_SEG_MAX))
        trace_virtio_blk_read_config_seg_max(_config.seg_max);
    if (get_guest_feature_bit(VIRTIO_BLK_F_GEOMETRY)) {
        trace_virtio_blk_read_config_geometry((u32)_config.geometry.cylinders, (u32)_config.geometry.heads, (u32)_config.geometry.sectors);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_BLK_SIZE))
        trace_virtio_blk_read_config_blk_size(_config.blk_size);
    if (get_guest_feature_bit(VIRTIO_BLK_F_TOPOLOGY)) {
        trace_virtio_blk_read_config_topology((u32)_config.physical_block_exp, (u32)_config.alignment_offset, (u32)_config.min_io_size, (u32)_config.opt_io_size);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_CONFIG_WCE))
        trace_virtio_blk_read_config_wce((u32)_config.wce);
    if (get_guest_feature_bit(VIRTIO_BLK_F_RO)) {
        set_readonly();
        trace_virtio_blk_read_config_ro();
    }
}

void blk::req_done()
{
    auto* queue = get_virt_queue(0);
    blk_req* req;

    while (1) {

        virtio_driver::wait_for_queue(queue, &vring::used_ring_not_empty);
        trace_virtio_blk_wake();

        u32 len;
        while((req = static_cast<blk_req*>(queue->get_buf_elem(&len))) != nullptr) {
            if (req->bio) {
                switch (req->res.status) {
                case VIRTIO_BLK_S_OK:
                    trace_virtio_blk_req_ok(req->bio, req->hdr.sector, req->bio->bio_bcount, req->hdr.type);
                    biodone(req->bio, true);
                    break;
                case VIRTIO_BLK_S_UNSUPP:
                    trace_virtio_blk_req_unsupp(req->bio, req->hdr.sector, req->bio->bio_bcount, req->hdr.type);
                    biodone(req->bio, false);
                    break;
                default:
                    trace_virtio_blk_req_err(req->bio, req->hdr.sector, req->bio->bio_bcount, req->hdr.type);
                    biodone(req->bio, false);
                    break;
               }
            }

            delete req;
            queue->get_buf_finalize();
        }

        // wake up the requesting thread in case the ring was full before
        queue->wakeup_waiter();
    }
}

int64_t blk::size()
{
    return _config.capacity * _config.blk_size;
}

static const int sector_size = 512;

int blk::make_request(struct bio* bio)
{
    // The lock is here for parallel requests protection
    WITH_LOCK(_lock) {

        if (!bio) return EIO;

        if (bio->bio_bcount/mmu::page_size + 1 > _config.seg_max) {
            trace_virtio_blk_make_request_seg_max(bio->bio_bcount, _config.seg_max);
            return EIO;
        }

        auto* queue = get_virt_queue(0);
        blk_request_type type;

        switch (bio->bio_cmd) {
        case BIO_READ:
            type = VIRTIO_BLK_T_IN;
            break;
        case BIO_WRITE:
            if (is_readonly()) {
                trace_virtio_blk_make_request_readonly();
                biodone(bio, false);
                return EROFS;
            }
            type = VIRTIO_BLK_T_OUT;
            break;
        case BIO_FLUSH:
            type = VIRTIO_BLK_T_FLUSH;
            break;
        default:
            return ENOTBLK;
        }

        auto* req = new blk_req(bio);
        blk_outhdr* hdr = &req->hdr;
        hdr->type = type;
        hdr->ioprio = 0;
        hdr->sector = bio->bio_offset / sector_size;

        queue->init_sg();
        queue->add_out_sg(hdr, sizeof(struct blk_outhdr));

        if (bio->bio_data && bio->bio_bcount > 0) {
            if (type == VIRTIO_BLK_T_OUT)
                queue->add_out_sg(bio->bio_data, bio->bio_bcount);
            else
                queue->add_in_sg(bio->bio_data, bio->bio_bcount);
        }

        req->res.status = 0;
        queue->add_in_sg(&req->res, sizeof (struct blk_res));

        queue->add_buf_wait(req);

        queue->kick();

        return 0;
    }
}

u32 blk::get_driver_features()
{
    auto base = virtio_driver::get_driver_features();
    return (base | ( 1 << VIRTIO_BLK_F_SIZE_MAX)
                 | ( 1 << VIRTIO_BLK_F_SEG_MAX)
                 | ( 1 << VIRTIO_BLK_F_GEOMETRY)
                 | ( 1 << VIRTIO_BLK_F_RO)
                 | ( 1 << VIRTIO_BLK_F_BLK_SIZE)
                 | ( 1 << VIRTIO_BLK_F_CONFIG_WCE)
                 | ( 1 << VIRTIO_BLK_F_WCE));
}

hw_driver* blk::probe(hw_device* dev)
{
    return virtio::probe<blk, VIRTIO_BLK_DEVICE_ID>(dev);
}

}
