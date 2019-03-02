/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <sys/cdefs.h>

#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/sched.hh>
#include <osv/interrupt.hh>
#include "drivers/virtio.hh"
#include "drivers/virtio-scsi.hh"
#include "drivers/scsi-common.hh"

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string.h>
#include <errno.h>

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/types.h>

using namespace memory;

namespace virtio {

int scsi::_instance = 0;

struct scsi_priv {
    devop_strategy_t strategy;
    scsi* drv;
    u16 target;
    u16 lun;
};

static void scsi_strategy(struct bio *bio)
{
    auto prv = scsi::get_priv(bio);
    prv->drv->make_request(bio);
}

static int scsi_read(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_read(dev, uio, ioflags);
}

static int scsi_write(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_write(dev, uio, ioflags);
}

static struct devops scsi_devops {
    no_open,
    no_close,
    scsi_read,
    scsi_write,
    no_ioctl,
    no_devctl,
    multiplex_strategy,
};

struct driver scsi_driver = {
    "virtio_scsi",
    &scsi_devops,
    sizeof(struct scsi_priv),
};

int scsi::exec_cmd(struct bio *bio)
{
    auto queue = get_virt_queue(VIRTIO_SCSI_QUEUE_REQ);
    auto req = static_cast<scsi_virtio_req*>(bio->bio_private);
    auto &req_cmd = req->req.cmd;
    auto &resp_cmd = req->resp.cmd;

    memcpy(req_cmd.cdb, req->cdb, req->cdb_len);

    queue->init_sg();
    queue->add_out_sg(&req_cmd, sizeof(req_cmd));
    if (cdb_data_in(req_cmd.cdb)) {
        queue->add_in_sg(&resp_cmd, sizeof(resp_cmd));
        if (bio->bio_data && bio->bio_bcount > 0)
            queue->add_in_sg(bio->bio_data, bio->bio_bcount);
    } else {
        if (bio->bio_data && bio->bio_bcount > 0)
            queue->add_out_sg(bio->bio_data, bio->bio_bcount);
        queue->add_in_sg(&resp_cmd, sizeof(resp_cmd));
    }

    queue->add_buf_wait(req);

    queue->kick();

    return 0;
}

void scsi::add_lun(u16 target, u16 lun)
{
    struct scsi_priv* prv;
    struct device *dev;
    size_t devsize;

    if (!test_lun(target, lun))
        return;

    exec_read_capacity(target, lun, devsize);

    std::string dev_name("vblk");
    dev_name += std::to_string(_disk_idx++);
    dev = device_create(&scsi_driver, dev_name.c_str(), D_BLK);
    prv = static_cast<struct scsi_priv*>(dev->private_data);
    prv->strategy = scsi_strategy;
    prv->drv = this;
    prv->target = target;
    prv->lun = lun;
    dev->size = devsize;
    dev->max_io_size = _config.max_sectors * VIRTIO_SCSI_SECTOR_SIZE;
    read_partition_table(dev);

    debug("virtio-scsi: Add scsi device target=%d, lun=%-3d as %s, devsize=%lld\n", target, lun, dev_name.c_str(), devsize);
}

bool scsi::ack_irq()
{
    auto isr = _dev.read_and_ack_isr();
    auto queue = get_virt_queue(VIRTIO_SCSI_QUEUE_REQ);

    if (isr) {
        queue->disable_interrupts();
        return true;
    } else {
        return false;
    }

}

scsi::scsi(virtio_device& dev)
    : virtio_driver(dev)
{

    _driver_name = "virtio-scsi";
    _id = _instance++;

    // Steps 4, 5 & 6 - negotiate and confirm features
    setup_features();
    read_config();

    // Step 7 - generic init of virtqueues
    probe_virt_queues();

    //register the single irq callback for the block
    sched::thread* t = sched::thread::make([this] { this->req_done(); },
            sched::thread::attr().name("virtio-scsi"));
    t->start();
    auto queue = get_virt_queue(VIRTIO_SCSI_QUEUE_REQ);

    interrupt_factory int_factory;
    int_factory.register_msi_bindings = [queue,t](interrupt_manager &msi) {
        msi.easy_register({
          { VIRTIO_SCSI_QUEUE_CTRL, nullptr, nullptr },
          { VIRTIO_SCSI_QUEUE_EVT, nullptr, nullptr },
          { VIRTIO_SCSI_QUEUE_REQ, [=] { queue->disable_interrupts(); }, t },
        });
    };

    int_factory.create_pci_interrupt = [this,t](pci::device &pci_dev) {
        return new pci_interrupt(
                pci_dev,
                [=] { return this->ack_irq(); },
                [=] { t->wake(); });
    };
    _dev.register_interrupt(int_factory);

    // Enable indirect descriptor
    queue->set_use_indirect(true);

    // Step 8
    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    scan();
}

scsi::~scsi()
{
    // TODO: cleanup resouces
}

void scsi::read_config()
{
    virtio_conf_read(0, &_config, sizeof(_config));
    config.max_lun = _config.max_lun;
    config.max_target = _config.max_target;
}

void scsi::req_done()
{
    auto queue = get_virt_queue(VIRTIO_SCSI_QUEUE_REQ);

    while (1) {

        virtio_driver::wait_for_queue(queue, &vring::used_ring_not_empty);

        scsi_virtio_req* req;
        u32 len;
        while ((req = static_cast<scsi_virtio_req*>(queue->get_buf_elem(&len))) != nullptr) {
            auto response = req->resp.cmd.response;
            auto status = req->resp.cmd.status;
            auto bio = req->bio;

            req->response = response;
            req->status = status;

            if (req->free_by_driver)
                delete req;

            biodone(bio, response == VIRTIO_SCSI_S_OK);
            queue->get_buf_finalize();
        }

        // wake up the requesting thread in case the ring was full before
        queue->wakeup_waiter();
    }
}

int scsi::make_request(struct bio* bio)
{
    WITH_LOCK(_lock) {
        if (!bio)
            return EIO;

        struct scsi_priv *prv;
        u16 target = 0, lun = 0;
        if (bio->bio_cmd != BIO_SCSI) {
            prv = scsi::get_priv(bio);
            target = prv->target;
            lun = prv->lun;
        }

        return handle_bio(target, lun, bio);
    }
}

u32 scsi::get_driver_features()
{
    auto base = virtio_driver::get_driver_features();
    return base | ( 1 << VIRTIO_SCSI_F_INOUT);
}

hw_driver* scsi::probe(hw_device* dev)
{
    return virtio::probe<scsi, VIRTIO_ID_SCSI>(dev);
}
}
