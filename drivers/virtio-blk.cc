/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <osv/drivers_config.h>
#include <sys/cdefs.h>

#include "drivers/blk-common.hh"
#include "drivers/virtio.hh"
#include "drivers/virtio-blk.hh"
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
#include "osv/aligned_new.hh"

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
TRACEPOINT(trace_virtio_blk_strategy, "write=%u, offset=%lu, bcount=%lu", bool, off_t, size_t);
TRACEPOINT(trace_virtio_blk_strategy_ret, "%d", int);
TRACEPOINT(trace_virtio_blk_req_ok, "bio=%p, sector=%lu, len=%lu, type=%x", struct bio*, u64, size_t, u32);
TRACEPOINT(trace_virtio_blk_req_unsupp, "bio=%p, sector=%lu, len=%lu, type=%x", struct bio*, u64, size_t, u32);
TRACEPOINT(trace_virtio_blk_req_err, "bio=%p, sector=%lu, len=%lu, type=%x", struct bio*, u64, size_t, u32);

using namespace memory;


namespace virtio {

int blk::_instance = 0;


struct blk_priv {
    devop_strategy_t strategy;
    blk* drv;
};

static void
blk_strategy(struct bio *bio)
{
    struct blk_priv *prv = reinterpret_cast<struct blk_priv*>(bio->bio_dev->private_data);

    trace_virtio_blk_strategy(bio->bio_cmd == BIO_WRITE, bio->bio_offset, bio->bio_bcount);
    prv->drv->make_request(bio);
    trace_virtio_blk_strategy_ret(0);
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
    blk_ioctl,
    no_devctl,
    multiplex_strategy,
};

struct driver blk_driver = {
    "virtio_blk",
    &blk_devops,
    sizeof(struct blk_priv),
};

bool blk::ack_irq()
{
    auto isr = _dev.read_and_ack_isr();
    auto queue = get_virt_queue(0);

    if (isr) {
        queue->disable_interrupts();
        return true;
    } else {
        return false;
    }

}

blk::blk(virtio_device& virtio_dev)
    : virtio_driver(virtio_dev), _ro(false), _num_queues(1), _queue_locks(1)
{
    _driver_name = "virtio-blk";
    _id = _instance++;
    virtio_i("VIRTIO BLK INSTANCE %d\n", _id);

    // Steps 4, 5 & 6 - negotiate and confirm features
    setup_features();
    read_config();

    // Step 7 - generic init of virtqueues
    probe_virt_queues();

    // Resize per-queue lock vector now that _num_queues is known.
    _queue_locks = std::vector<mutex>(_num_queues);

    // Create one completion thread per virtqueue.  Each thread drains its
    // own queue ring; the interrupt wakes queue-0's thread which then also
    // polls the remaining queues (simple single-interrupt multiqueue model).
    for (int qid = 0; qid < _num_queues; qid++) {
        std::string tname = std::string("virtio-blk") +
                            (_num_queues > 1 ? "/q" + std::to_string(qid) : "");
        // 256 KB stack (matches the ZFS kthread convention in
        // bsd/porting/kthread.cc): bio completion runs the filesystem's
        // bio_done callback inline, and the ZFS path (vdev_disk_bio_done)
        // overruns the default 64 KB kernel stack.
        sched::thread* t = sched::thread::make(
            [this, qid] { this->req_done(qid); },
            sched::thread::attr().name(tname).stack(256 << 10));
        t->start();
        if (qid == 0) {
            // Only queue-0's thread is woken by the interrupt.
            auto queue = get_virt_queue(0);
            interrupt_factory int_factory;
#if CONF_drivers_pci
            int_factory.register_msi_bindings = [queue, t](interrupt_manager &msi) {
                msi.easy_register( {{ 0, [=] { queue->disable_interrupts(); }, t }});
            };

            int_factory.create_pci_interrupt = [this,t](pci::device &pci_dev) {
                return new pci_interrupt(
                    pci_dev,
                    [=] { return this->ack_irq(); },
                    [=] { t->wake_with_irq_disabled(); });
            };
#endif

#if CONF_drivers_mmio
#ifdef __aarch64__
            int_factory.create_spi_edge_interrupt = [this,t]() {
                return new spi_interrupt(
                    gic::irq_type::IRQ_TYPE_EDGE,
                    _dev.get_irq(),
                    [=] { return this->ack_irq(); },
                    [=] { t->wake_with_irq_disabled(); });
            };
#else
            int_factory.create_gsi_edge_interrupt = [this,t]() {
                return new gsi_edge_interrupt(
                    _dev.get_irq(),
                    [=] { if (this->ack_irq()) t->wake_with_irq_disabled(); });
            };
#endif
#endif
            _dev.register_interrupt(int_factory);

            // Enable indirect descriptor
            queue->set_use_indirect(true);
        }
    }
    // Step 8
    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    struct blk_priv* prv;
    struct device *dev;
    std::string dev_name("vblk");
    dev_name += std::to_string(_disk_idx++);

    dev = device_create(&blk_driver, dev_name.c_str(), D_BLK);
    prv = reinterpret_cast<struct blk_priv*>(dev->private_data);
    prv->strategy = blk_strategy;
    prv->drv = this;
    dev->size = prv->drv->size();
    dev->max_io_size = _config.seg_max ? (_config.seg_max - 1) * mmu::page_size : UINT_MAX;
    read_partition_table(dev);

    debugf("virtio-blk: Add blk device instances %d as %s, devsize=%lld\n", _id, dev_name.c_str(), dev->size);
}

blk::~blk()
{
    //TODO: In theory maintain the list of free instances and gc it
    // including the thread objects and their stack
}

#define READ_CONFIGURATION_FIELD(config,field_name,field) \
    virtio_conf_read(offsetof(config,field_name), &field, sizeof(field));

void blk::read_config()
{
    READ_CONFIGURATION_FIELD(blk_config,capacity,_config.capacity)
    trace_virtio_blk_read_config_capacity(_config.capacity);

    if (get_guest_feature_bit(VIRTIO_BLK_F_SIZE_MAX)) {
        READ_CONFIGURATION_FIELD(blk_config,size_max,_config.size_max)
        trace_virtio_blk_read_config_size_max(_config.size_max);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_SEG_MAX)) {
        READ_CONFIGURATION_FIELD(blk_config,seg_max,_config.seg_max)
        trace_virtio_blk_read_config_seg_max(_config.seg_max);
    } else {
        _config.seg_max = 0;
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_GEOMETRY)) {
        READ_CONFIGURATION_FIELD(blk_config,geometry,_config.geometry)
        trace_virtio_blk_read_config_geometry((u32)_config.geometry.cylinders, (u32)_config.geometry.heads, (u32)_config.geometry.sectors);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_BLK_SIZE)) {
        READ_CONFIGURATION_FIELD(blk_config,blk_size,_config.blk_size)
        trace_virtio_blk_read_config_blk_size(_config.blk_size);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_TOPOLOGY)) {
        READ_CONFIGURATION_FIELD(blk_config,topology,_config.topology)
        trace_virtio_blk_read_config_topology((u32)_config.topology.physical_block_exp, (u32)_config.topology.alignment_offset,
          (u32)_config.topology.min_io_size, (u32)_config.topology.opt_io_size);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_CONFIG_WCE))
        trace_virtio_blk_read_config_wce((u32)_config.wce);
    if (get_guest_feature_bit(VIRTIO_BLK_F_RO)) {
        set_readonly();
        trace_virtio_blk_read_config_ro();
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_MQ)) {
        READ_CONFIGURATION_FIELD(blk_config,num_queues,_config.num_queues)
        _num_queues = _config.num_queues;
        virtio_i("virtio-blk: multiqueue with %d queues\n", _num_queues);
    }
    if (get_guest_feature_bit(VIRTIO_BLK_F_DISCARD)) {
        READ_CONFIGURATION_FIELD(blk_config,max_discard_sectors,_config.max_discard_sectors)
        READ_CONFIGURATION_FIELD(blk_config,max_discard_seg,_config.max_discard_seg)
        READ_CONFIGURATION_FIELD(blk_config,discard_sector_alignment,_config.discard_sector_alignment)
        virtio_i("virtio-blk: DISCARD support enabled (max_sectors=%u, max_seg=%u, alignment=%u)\n",
                _config.max_discard_sectors, _config.max_discard_seg, _config.discard_sector_alignment);
    }
}

/*
 * drain_queue() — pull all completed requests off one virtqueue ring.
 * Returns the number of completions processed.
 */
int blk::drain_queue(vring* queue)
{
    int n = 0;
    u32 len;
    blk_req* req;

    while ((req = static_cast<blk_req*>(queue->get_buf_elem(&len))) != nullptr) {
        if (req->bio) {
            switch (req->res.status) {
            case VIRTIO_BLK_S_OK:
                trace_virtio_blk_req_ok(req->bio, req->hdr.sector,
                                        req->bio->bio_bcount, req->hdr.type);
                biodone(req->bio, true);
                break;
            case VIRTIO_BLK_S_UNSUPP:
                trace_virtio_blk_req_unsupp(req->bio, req->hdr.sector,
                                            req->bio->bio_bcount, req->hdr.type);
                biodone(req->bio, false);
                break;
            default:
                trace_virtio_blk_req_err(req->bio, req->hdr.sector,
                                         req->bio->bio_bcount, req->hdr.type);
                biodone(req->bio, false);
                break;
            }
        }
        delete req;
        queue->get_buf_finalize();
        n++;
    }
    return n;
}

/*
 * req_done() — completion thread for virtqueue @qid.
 *
 * For qid==0 (the interrupt-driven queue), the thread sleeps until woken by
 * the IRQ handler, then drains ALL queues so that in the common single-
 * interrupt multiqueue configuration every queue gets serviced promptly.
 *
 * For qid>0 the thread polls its own queue in a tight loop, yielding between
 * rounds.  This is suboptimal for the rare case where the hypervisor supports
 * per-queue MSI-X vectors; the simple single-interrupt model used here means
 * the queue-N threads act as cooperative drain helpers woken by queue-0.
 */
void blk::req_done(int qid)
{
    auto* myqueue = get_virt_queue(qid);

    while (1) {
        if (qid == 0) {
            /* Wait for the interrupt to signal any activity. */
            virtio_driver::wait_for_queue(myqueue, &vring::used_ring_not_empty);
            trace_virtio_blk_wake();

            /* Drain all queues while we're awake.
             * Hold each queue's lock so we don't race with the polling
             * threads (qid > 0) that also call drain_queue on their queue. */
            for (int q = 0; q < _num_queues; q++) {
                WITH_LOCK(_queue_locks[q]) {
                    auto* q_ring = get_virt_queue(q);
                    drain_queue(q_ring);
                    q_ring->wakeup_waiter();
                }
            }
        } else {
            /* Non-zero queues: poll, but hold the per-queue lock so we
             * don't race with qid=0's drain-all loop above. */
            WITH_LOCK(_queue_locks[qid]) {
                if (!drain_queue(myqueue)) {
                    // nothing processed; release lock before yielding
                } else {
                    myqueue->wakeup_waiter();
                }
            }
            sched::thread::yield();
        }
    }
}

static const int sector_size = 512;

int64_t blk::size()
{
    return _config.capacity * sector_size;
}

int blk::make_request(struct bio* bio)
{
    if (!bio) return EIO;

    if (get_guest_feature_bit(VIRTIO_BLK_F_SEG_MAX)) {
        if (bio->bio_bcount/mmu::page_size + 1 > _config.seg_max) {
            trace_virtio_blk_make_request_seg_max(bio->bio_bcount, _config.seg_max);
            return EIO;
        }
    }

    /* Select a queue by CPU id so parallel CPUs use independent rings. */
    int qid = sched::cpu::current()->id % _num_queues;

    WITH_LOCK(_queue_locks[qid]) {

        auto* queue = get_virt_queue(qid);
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
        case BIO_DISCARD:
            if (!get_guest_feature_bit(VIRTIO_BLK_F_DISCARD)) {
                biodone(bio, false);
                return EOPNOTSUPP;
            }
            type = VIRTIO_BLK_T_DISCARD;
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

        if (type == VIRTIO_BLK_T_DISCARD) {
            req->discard_desc.sector = bio->bio_offset / sector_size;
            req->discard_desc.num_sectors = bio->bio_bcount / sector_size;
            req->discard_desc.flags = 0;
            queue->add_out_sg(&req->discard_desc, sizeof(req->discard_desc));
        } else if (bio->bio_data && bio->bio_bcount > 0) {
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

u64 blk::get_driver_features()
{
    auto base = virtio_driver::get_driver_features();
    return (base | ( 1 << VIRTIO_BLK_F_SIZE_MAX)
                 | ( 1 << VIRTIO_BLK_F_SEG_MAX)
                 | ( 1 << VIRTIO_BLK_F_GEOMETRY)
                 | ( 1 << VIRTIO_BLK_F_RO)
                 | ( 1 << VIRTIO_BLK_F_BLK_SIZE)
                 | ( 1 << VIRTIO_BLK_F_CONFIG_WCE)
                 | ( 1 << VIRTIO_BLK_F_WCE)
                 | ( 1 << VIRTIO_BLK_F_MQ)
                 | ( 1 << VIRTIO_BLK_F_DISCARD));
}

hw_driver* blk::probe(hw_device* dev)
{
    return virtio::probe<blk, VIRTIO_ID_BLOCK>(dev);
}

}
