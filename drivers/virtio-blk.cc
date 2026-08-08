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

    // read_config() recorded the device's advertised num_queues, but
    // probe_virt_queues() determines how many virtqueues actually exist and
    // stores that in the base class _num_queues.
    // Use the minimum of probed count and number of cpus as the
    // authoritative value so make_request()/req_done() never index a queue the
    // base class did not set up. Also, there is no point of creating more queues
    // than number of cpus, as make_request() would never submit requests there.
    _num_queues = std::min<u32>(virtio_driver::_num_queues, sched::cpus.size());

    // Resize per-queue lock vector and create as many threads
    // now that _num_queues is known.
    _queue_locks = std::vector<mutex>(_num_queues);
    auto threads = std::vector<sched::thread*>(_num_queues);

    // Enable indirect descriptors on every queue so large multi-segment
    // requests use a single descriptor entry on whichever queue
    // make_request() selects.
    bool multi_queue = _num_queues > 1;
    for (int qid = 0; qid < _num_queues; qid++) {
        get_virt_queue(qid)->set_use_indirect(true);
        auto *t = sched::thread::make(
           [this,qid] { this->req_done(qid); },
           sched::thread::attr().name("virtio-blk" + (_num_queues > 1 ? std::to_string(qid) : "") + "_req_done"));
        t->start();
        // It only makes sense and is actuall necessary to pin the consumer
        // threads when there are more than one queue so that each is serviced
        // on different cpu to make it thread-safe
        if (multi_queue) {
           auto cpu = sched::cpus[qid % sched::cpus.size()];
           sched::thread::pin(t, cpu);
        }
        threads[qid] = t;
    }
    //
    // Use 1st thread when only single queue is available
    auto single_thread = threads[0];

    // With VIRTIO_BLK_F_MQ, setup_queue() maps queue index i -> MSI-X entry i
    // (1:1), so a completion on queue i raises entry i's interrupt, not queue
    // 0's; register an ISR for every queue's vector or completions on queues
    // 1..N-1 are never serviced and I/O steered there hangs. Each ISR disables
    // its own queue's interrupts and wakes its dedicated completion thread, which
    // then drains the queue. (The config-change MSI-X vector is left at
    // VIRTIO_MSI_NO_VECTOR, as it is for all OSv virtio devices - virtio-blk
    // reads capacity via the config space, not a config-change interrupt.)
    interrupt_factory int_factory;
#if CONF_drivers_pci
    int_factory.register_msi_bindings = [this, threads](interrupt_manager &msi) {
        std::vector<msix_binding> bindings;
        bindings.reserve(_num_queues);
        for (int qid = 0; qid < _num_queues; qid++) {
            // Use a dedicated thread to be woken up when
            // an interrupt for the relevant queue is delivered
            auto* q = get_virt_queue(qid);
            auto* t = threads[qid];
            bindings.push_back({ (unsigned)qid, [q,t] { q->disable_interrupts(); }, t });
        }
        // easy_register() needs one MSI-X vector per binding; if the device
        // advertised more queues than it has MSI-X entries it returns false
        // having registered NONE, which would silently hang all I/O. Fail
        // loudly at probe instead - a device that negotiates F_MQ is expected
        // to expose at least num_queues (+1 config) vectors.
        if (!msi.easy_register(bindings)) {
            virtio_e("virtio-blk: failed to register MSI-X vectors for %d queues "
                     "(device MSI-X entries insufficient)", _num_queues);
            abort();
        }
    };

    int_factory.create_pci_interrupt = [this,single_thread](pci::device &pci_dev) {
        return new pci_interrupt(
            pci_dev,
            [=] { return this->ack_irq(); },
            [=] { single_thread->wake_with_irq_disabled(); });
    };
#endif

#if CONF_drivers_mmio
#ifdef __aarch64__
    int_factory.create_spi_edge_interrupt = [this,single_thread]() {
        return new spi_interrupt(
            gic::irq_type::IRQ_TYPE_EDGE,
            _dev.get_irq(),
            [=] { return this->ack_irq(); },
            [=] { single_thread->wake_with_irq_disabled(); });
    };
#else
    int_factory.create_gsi_edge_interrupt = [this,single_thread]() {
        return new gsi_edge_interrupt(
            _dev.get_irq(),
            [=] { if (this->ack_irq()) single_thread->wake_with_irq_disabled(); });
    };
#endif
#endif
    _dev.register_interrupt(int_factory);
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

    debugf("virtio-blk: Add blk device instances %d as %s with %d queue%s, devsize=%lld\n",
        _id, dev_name.c_str(), _num_queues, (_num_queues > 1 ? "s" : ""), dev->size);
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
        virtio_i("virtio-blk: multiqueue, device advertises %d queues\n", _config.num_queues);
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
 * req_done() — completion thread for single virtqueue.
 *
 * virtio-blk uses one interrupt per queue, so a completion landing on that queue
 * wakes its thread. It sleeps until this queue's used ring is non-empty,
 * then drains it. No lock needs to be held while the queue is drained,
 * as the req_done() for each queue is handled on different cpu, and also
 * is thread-safe when interacting with producer threads calling make_request()
 * because it races only on the lock-free _used_ring_host_head counter.
 */
void blk::req_done(int qid)
{
    auto* queue = get_virt_queue(qid);
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

static const int sector_size = 512;

int64_t blk::size()
{
    return _config.capacity * sector_size;
}

int blk::make_request(struct bio* bio)
{
    if (!bio) return EIO;

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
            // The discard range must be sector-aligned; silently rounding it
            // down would discard a different range than the caller asked for.
            if (bio->bio_bcount == 0 ||
                (bio->bio_offset % sector_size) != 0 ||
                (bio->bio_bcount % sector_size) != 0) {
                biodone(bio, false);
                return EINVAL;
            }
            // num_sectors is a u32 in the virtio descriptor and the device
            // advertises an upper bound via max_discard_sectors.  Reject a
            // range that would overflow the field or exceed the device limit
            // rather than truncating it and discarding the wrong amount.
            {
                u64 nsectors = bio->bio_bcount / sector_size;
                u32 max_sectors = _config.max_discard_sectors ?
                    _config.max_discard_sectors : UINT32_MAX;
                if (nsectors > max_sectors) {
                    biodone(bio, false);
                    return EINVAL;
                }
            }
            type = VIRTIO_BLK_T_DISCARD;
            break;
        default:
            biodone(bio, false);
            return ENOTBLK;
        }

        // SEG_MAX bounds the number of data segments a request may span, so it
        // only applies to requests that carry a data payload (READ/WRITE).
        // FLUSH and DISCARD add no data SG, so skip the check for them.
        if ((type == VIRTIO_BLK_T_IN || type == VIRTIO_BLK_T_OUT) &&
            get_guest_feature_bit(VIRTIO_BLK_F_SEG_MAX)) {
            if (bio->bio_bcount/mmu::page_size + 1 > _config.seg_max) {
                trace_virtio_blk_make_request_seg_max(bio->bio_bcount, _config.seg_max);
                biodone(bio, false);
                return EIO;
            }
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
