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
#include "drivers/pci-device.hh"
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
    scsi* drv;
    u16 target;
    u16 lun;
};

static void scsi_strategy(struct bio *bio)
{
    auto prv = scsi::get_priv(bio);
    bio->bio_offset += bio->bio_dev->offset;
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
    scsi_strategy,
};

struct driver scsi_driver = {
    "virtio_scsi",
    &scsi_devops,
    sizeof(struct scsi_priv),
};

bool scsi::cdb_data_in(const u8 *cdb)
{
   return cdb[0] != CDB_CMD_WRITE_16;
}

int scsi::exec_cmd(struct bio *bio)
{
    auto queue = get_virt_queue(VIRTIO_SCSI_QUEUE_REQ);
    auto req = static_cast<scsi_req*>(bio->bio_private);
    auto &req_cmd = req->req.cmd;
    auto &resp_cmd = req->resp.cmd;

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

int scsi::exec_readwrite(struct bio *bio, u8 cmd)
{
    auto prv = scsi::get_priv(bio);
    new scsi_req(bio, prv->target, prv->lun, cmd);

    return exec_cmd(bio);
}

int scsi::exec_synccache(struct bio *bio, u8 cmd)
{
    auto prv = get_priv(bio);
    new scsi_req(bio, prv->target, prv->lun, cmd);

    return exec_cmd(bio);
}

std::vector<u16> scsi::exec_report_luns(u16 target)
{
    std::vector<u16> luns;
    struct bio *bio = alloc_bio();
    if (!bio)
        throw std::runtime_error("Fail to allocate bio");

    auto req = new scsi_req(bio, target, 0);
    auto data = new cdbres_report_luns;

    bio->bio_bcount = sizeof(*data);
    bio->bio_data = data;

    struct cdb_report_luns cdb;
    memset(&cdb, 0, sizeof(cdb));
    cdb.command = CDB_CMD_REPORT_LUNS;
    cdb.select_report = 0;
    cdb.alloc_len=htobe32(sizeof(*data));
    memcpy(req->req.cmd.cdb, &cdb, sizeof(cdb));

    make_request(bio);
    bio_wait(bio);
    destroy_bio(bio);

    auto response = req->resp.cmd.response;
    if (response != VIRTIO_SCSI_S_OK)
        throw std::runtime_error("Fail to exec_report_luns");

    auto list_len = be32toh(data->list_len);
    for (unsigned i = 0; i < list_len / 8; i++)
        luns.push_back((data->list[i] & 0xffff) >> 8);
    std::sort(luns.begin(),luns.end());

    delete req;
    delete data;

    return luns;
}

void scsi::exec_inquery(u16 target, u16 lun)
{
    struct bio *bio = alloc_bio();
    if (!bio)
        throw std::runtime_error("Fail to alloate bio");

    auto req = new scsi_req(bio, target, lun);
    auto data = new cdbres_inquiry;

    bio->bio_bcount = sizeof(*data);
    bio->bio_data = data;

    struct cdb_inquery cdb;
    memset(&cdb, 0, sizeof(cdb));
    cdb.command = CDB_CMD_INQUIRY;
    cdb.alloc_len = htobe16(sizeof(*data));
    memcpy(req->req.cmd.cdb, &cdb, sizeof(cdb));

    make_request(bio);
    bio_wait(bio);
    destroy_bio(bio);

    auto response = req->resp.cmd.response;
    if (response != VIRTIO_SCSI_S_OK)
        throw std::runtime_error("Fail to exec_inquery");
}

void scsi::exec_read_capacity(u16 target, u16 lun, size_t &devsize)
{
    struct bio *bio = alloc_bio();
    if (!bio)
        throw std::runtime_error("Fail to allocate bio");

    auto req = new scsi_req(bio, target, lun);
    auto data = new cdbres_read_capacity;

    bio->bio_bcount = sizeof(*data);
    bio->bio_data = data;

    struct cdb_read_capacity cdb;
    memset(&cdb, 0, sizeof(cdb));
    cdb.command = CDB_CMD_READ_CAPACITY;
    cdb.service_action = 0x10;
    cdb.alloc_len = htobe32(sizeof(*data));
    memcpy(req->req.cmd.cdb, &cdb, sizeof(cdb));

    make_request(bio);
    bio_wait(bio);
    destroy_bio(bio);

    // sectors returned by this cmd is the address of laster sector
    u64 sectors = be64toh(data->sectors) + 1;
    u32 blksize = be32toh(data->blksize);
    devsize = sectors * blksize;

    auto response = req->resp.cmd.response;
    if (response != VIRTIO_SCSI_S_OK)
        throw std::runtime_error("Fail to exec_read_capacity");

    delete req;
    delete data;
}

void scsi::exec_test_unit_ready(u16 target, u16 lun)
{
    struct bio *bio = alloc_bio();
    if (!bio)
        throw std::runtime_error("Fail to allocate bio");

    auto req = new scsi_req(bio, target, lun);

    struct cdb_test_unit_ready cdb;
    memset(&cdb, 0, sizeof(cdb));
    cdb.command = CDB_CMD_TEST_UNIT_READY;
    memcpy(req->req.cmd.cdb, &cdb, sizeof(cdb));

    make_request(bio);
    bio_wait(bio);
    destroy_bio(bio);

    auto response = req->resp.cmd.response;
    if (response != VIRTIO_SCSI_S_OK)
        throw std::runtime_error("Fail to exec_test_unit_ready");

    delete req;
}

void scsi::exec_request_sense(u16 target, u16 lun)
{

    struct bio *bio = alloc_bio();
    if (!bio)
        throw std::runtime_error("Fail to allocate bio");

    auto req = new scsi_req(bio, target, lun);
    auto data = new cdbres_request_sense;

    bio->bio_bcount = sizeof(*data);
    bio->bio_data = data;

    struct cdb_request_sense cdb;
    memset(&cdb, 0, sizeof(cdb));
    cdb.command = CDB_CMD_REQUEST_SENSE;
    cdb.alloc_len = sizeof(*data);
    memcpy(req->req.cmd.cdb, &cdb, sizeof(cdb));

    make_request(bio);
    bio_wait(bio);
    destroy_bio(bio);

    if (data->asc == 0x3a)
        printf("virtio-scsi: target %d lun %d reports medium not present\n", target, lun);

    auto response = req->resp.cmd.response;
    if (response != VIRTIO_SCSI_S_OK)
        throw std::runtime_error("Fail to exec_request_sense");

    delete req;
    delete data;
}


void scsi::add_lun(u16 target, u16 lun)
{
    struct scsi_priv* prv;
    struct device *dev;
    size_t devsize;

    exec_inquery(target, lun);

    try {
        exec_test_unit_ready(target, lun);
    } catch (std::runtime_error err) {
        printf("virtio-scsi: %s\n", err.what());
        exec_request_sense(target, lun);
        return;
    }

    exec_read_capacity(target, lun, devsize);

    std::string dev_name("vblk");
    dev_name += std::to_string(_disk_idx++);
    dev = device_create(&scsi_driver, dev_name.c_str(), D_BLK);
    prv = static_cast<struct scsi_priv*>(dev->private_data);
    prv->drv = this;
    prv->target = target;
    prv->lun = lun;
    dev->size = devsize;
    read_partition_table(dev);

    printf("virtio-scsi: Add scsi device target=%d, lun=%-3d as %s, devsize=%lld\n", target, lun, dev_name.c_str(), devsize);

}


void scsi::scan()
{
    /* TODO: Support more target */
    for (u16 target = 0; target < 1; target++) {
        try {
            auto luns = exec_report_luns(target);
            for (auto &lun : luns) {
                add_lun(target, lun);
            }
        } catch(std::runtime_error err) {
            printf("virtio-scsi: %s\n", err.what());
        }
    }
}

scsi::scsi(pci::device& dev)
    : virtio_driver(dev)
{

    _driver_name = "virtio-scsi";
    _id = _instance++;

    setup_features();
    read_config();

    //register the single irq callback for the block
    sched::thread* t = new sched::thread([this] { this->req_done(); },
            sched::thread::attr().name("virtio-scsi"));
    t->start();
    auto queue = get_virt_queue(VIRTIO_SCSI_QUEUE_REQ);
    _msi.easy_register({
            { VIRTIO_SCSI_QUEUE_CTRL, nullptr, nullptr },
            { VIRTIO_SCSI_QUEUE_EVT, nullptr, nullptr },
            { VIRTIO_SCSI_QUEUE_REQ, [=] { queue->disable_interrupts(); }, t },
    });

    // Enable indirect descriptor
    queue->set_use_indirect(true);

    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    scan();
}

scsi::~scsi()
{
    // TODO: cleanup resouces
}

scsi::scsi_req::scsi_req(struct bio* bio, u16 target, u16 lun, u8 cmd)
    : bio(bio)
{

    init(bio, target, lun);
    switch (cmd) {
        case CDB_CMD_READ_16:
        case CDB_CMD_WRITE_16: {
           u64 lba = bio->bio_offset / VIRTIO_SCSI_SECTOR_SIZE;
           u32 count = bio->bio_bcount / VIRTIO_SCSI_SECTOR_SIZE;
           struct cdb_readwrite_16 cdb;
           memset(&cdb, 0, sizeof(cdb));
           cdb.command = cmd;
           cdb.lba = htobe64(lba);
           cdb.count = htobe32(count);
           memcpy(this->req.cmd.cdb, &cdb, sizeof(cdb));
           break;
        }
        case CDB_CMD_SYNCHRONIZE_CACHE_10: {
           struct cdb_readwrite_10 cdb;
           memset(&cdb, 0, sizeof(cdb));
           cdb.command = cmd;
           cdb.lba = 0;
           cdb.count = 0;
           memcpy(this->req.cmd.cdb, &cdb, sizeof(cdb));
           break;
        }
        default:
            break;
    };
}

bool scsi::read_config()
{
    virtio_conf_read(virtio_pci_config_offset(), &_config, sizeof(_config));
    return true;
}

void scsi::req_done()
{
    auto queue = get_virt_queue(VIRTIO_SCSI_QUEUE_REQ);

    while (1) {

        virtio_driver::wait_for_queue(queue, &vring::used_ring_not_empty);

        scsi_req* req;
        u32 len;
        while ((req = static_cast<scsi_req*>(queue->get_buf_elem(&len))) != nullptr) {
            auto response = req->resp.cmd.response;
            auto bio = req->bio;

            assert(req->resp.cmd.response == VIRTIO_SCSI_S_OK);

            // Other req type will be freed by the caller who send the bio
            if (req->bio->bio_cmd != BIO_SCSI)
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

        switch (bio->bio_cmd) {
        case BIO_READ:
            exec_readwrite(bio, CDB_CMD_READ_16);
            break;
        case BIO_WRITE:
            exec_readwrite(bio, CDB_CMD_WRITE_16);
            break;
        case BIO_FLUSH:
            exec_synccache(bio, CDB_CMD_SYNCHRONIZE_CACHE_10);
            break;
        case BIO_SCSI:
            exec_cmd(bio);
            break;
        default:
            return ENOTBLK;
        }
    }
    return 0;
}

u32 scsi::get_driver_features()
{
    auto base = virtio_driver::get_driver_features();
    return base | ( 1 << VIRTIO_SCSI_F_INOUT);
}

hw_driver* scsi::probe(hw_device* dev)
{
    return virtio::probe<scsi, VIRTIO_SCSI_DEVICE_ID>(dev);
}

}
