/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/types.h>
#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/sched.hh>
#include <osv/interrupt.hh>

#include "drivers/pci-device.hh"
#include "drivers/scsi-common.hh"
#include "drivers/vmw-pvscsi.hh"

#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <string.h>

using namespace memory;

namespace vmw {
int pvscsi::_instance = 0;
int pvscsi::_disk_idx = 0;

struct pvscsi_priv {
    devop_strategy_t strategy;
    pvscsi* drv;
    u16 target;
    u16 lun;
};

static void pvscsi_strategy(struct bio *bio)
{
    auto prv = pvscsi::get_priv(bio);
    prv->drv->make_request(bio);
}

static int pvscsi_read(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_read(dev, uio, ioflags);
}

static int pvscsi_write(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_write(dev, uio, ioflags);
}

static struct devops pvscsi_devops {
    no_open,
    no_close,
    pvscsi_read,
    pvscsi_write,
    no_ioctl,
    no_devctl,
    multiplex_strategy,
};

struct driver pvscsi_driver = {
    "vmw_pvscsi",
    &pvscsi_devops,
    sizeof(struct pvscsi_priv),
};

bool pvscsi::avail_desc()
{
    return _req_free.load(std::memory_order_relaxed) >= 1;
}

void pvscsi::kick_desc(u8 cdb)
{
    if (cdb_data_rw(&cdb))
        writel(pvscsi_reg_off::kick_rw_io, 0);
    else
        writel(pvscsi_reg_off::kick_non_rw_io, 0);
}

bool pvscsi::add_desc(struct bio *bio)
{
    auto s = _ring_state;
    if (!avail_desc()) {
        return false;
    }

    _req_free--;

    u32 req_entries = s->req_nr;
    auto desc = _ring_req + (s->req_prod_idx & mask(req_entries));
    auto req = static_cast<scsi_pvscsi_req*>(bio->bio_private);
    u8 cmd = req->cdb[0];
    desc->bus = 0;
    desc->target = req->target;
    memset(desc->lun, 0, sizeof(desc->lun));
    desc->lun[1] = req->lun;
    desc->sense_len = 0;
    desc->sense_addr = 0;
    desc->cdb_len = config.cdb_size;
    desc->vcpu_hint = 0;
    memcpy(desc->cdb, req->cdb, config.cdb_size);
    desc->tag = PVSCSI_SIMPLE_QUEUE_TAG;
    auto flags = cdb_data_in(req->cdb) ?  pvscsi_cmd_flag::dir_tohost : pvscsi_cmd_flag::dir_todevice;
    desc->flags = static_cast<u32>(flags);
    desc->context = reinterpret_cast<u64>(req);

    desc->data_len = bio->bio_bcount;
    desc->data_addr = bio->bio_data ? mmu::virt_to_phys(bio->bio_data) : 0;

    barrier();
    s->req_prod_idx++;

    kick_desc(cmd);

    return true;
}

void pvscsi::add_desc_wait(struct bio *bio)
{
    while (!add_desc(bio)) {
        _waiter.reset(*sched::thread::current());
        sched::thread::wait_until([this] {return this->avail_desc();});
        _waiter.clear();
    }
}

int pvscsi::exec_cmd(struct bio *bio)
{
    add_desc_wait(bio);
    return 0;
}

void pvscsi::dump_config()
{
    u8 B, D, F;

    _pci_dev.get_bdf(B, D, F);

    _pci_dev.dump_config();
}

bool pvscsi::parse_pci_config()
{
    if (!_pci_dev.parse_pci_config()) {
        return false;
    }

    for (int i = 1; i <= 6; i++) {
        _bar = _pci_dev.get_bar(i);
        // Find the first mmio bar
        if (_bar == nullptr || _bar->is_pio()) {
            continue;
        }
        _bar->map();
        return true;
    }

    return false;
}

void pvscsi::write_cmd_desc(pvscsi_cmd cmd, void *desc, size_t len)
{
    auto ptr = static_cast<u32 *>(desc);
    size_t i;
    len /= sizeof(*ptr);

    writel(pvscsi_reg_off::command, static_cast<u32>(cmd));
    for (i = 0; i < len; i++)
        writel(pvscsi_reg_off::command_data, ptr[i]);
}

void pvscsi::setup()
{
    auto page_size = mmu::page_size;
    auto page_size_shift = mmu::page_size_shift;

    write_cmd_desc(pvscsi_cmd::adapter_reset, NULL, 0);

    _ring_state = reinterpret_cast<pvscsi_ring_state *>(
                memory::alloc_phys_contiguous_aligned(page_size, page_size));
    _ring_state_pa = mmu::virt_to_phys(_ring_state);
    memset(_ring_state, 0, page_size);

    _ring_req = reinterpret_cast<pvscsi_ring_req_desc *>(
                memory::alloc_phys_contiguous_aligned(page_size, page_size));
    _ring_req_pa = mmu::virt_to_phys(_ring_req);
    memset(_ring_req, 0, page_size);

    _ring_cmp = reinterpret_cast<pvscsi_ring_cmp_desc *>(
                memory::alloc_phys_contiguous_aligned(page_size, page_size));
    _ring_cmp_pa = mmu::virt_to_phys(_ring_cmp);
    memset(_ring_cmp, 0, page_size);

    pvscsi_cmd_desc_setup_rings cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.req_ring_pages = 1;
    cmd.cmp_ring_pages = 1;
    cmd.ring_state_ppn = _ring_state_pa >> page_size_shift;
    cmd.req_ring_ppn[0] = _ring_req_pa >> page_size_shift;
    cmd.cmp_ring_ppn[0] = _ring_cmp_pa >> page_size_shift;

    write_cmd_desc(pvscsi_cmd::setup_rings, &cmd, sizeof(cmd));

    // Queue depth
    _req_depth = _req_free = page_size / sizeof(pvscsi_ring_req_desc);
}

pvscsi::pvscsi(pci::device& pci_dev)
    : hw_driver()
    , _pci_dev(pci_dev)
    , _msi(&pci_dev)
{

    _id = _instance++;
    _driver_name = "vmw-pvscsi";

    parse_pci_config();

    // PVSCSI supports only 1 lun per target
    config.max_lun = 1;

    // PVSCSI supports target 0 to target 15
    config.max_target = 16;

    pci_dev.set_bus_master(true);

    setup();

    //register the single irq callback for the block
    sched::thread* t = new sched::thread([this] { this->req_done(); },
            sched::thread::attr().name("vmw-pvscsi"));
    t->start();
    if (pci_dev.is_msix() || pci_dev.is_msi()) {
        _msi.easy_register({ { 0, [] { }, t }, });
    } else {
        abort("vmw-pvscsi msix is not present\n");
    }

    // Enable interrupt
    writel(pvscsi_reg_off::intr_mask, PVSCSI_INTR_CMPL_MASK);

    scan();
}

pvscsi::~pvscsi()
{
    // TODO: cleanup resouces
}

void pvscsi::req_done()
{
    auto s = _ring_state;
    pvscsi_ring_cmp_desc *desc;
    while (1) {
        sched::thread::wait_until([=] { return s->cmp_cons_idx != s->cmp_prod_idx;});
        u32 cmp_entries = s->cmp_nr;
        while (s->cmp_cons_idx != s->cmp_prod_idx) {
            desc = _ring_cmp + (s->cmp_cons_idx & mask(cmp_entries));
            barrier();

            auto req = reinterpret_cast<scsi_pvscsi_req*>(desc->context);
            auto bio = req->bio;

            // SAM defined
            req->status = desc->scsi_status;
            // PVSCSI defined
            req->response = desc->host_status;

            auto response = req->response;

            if (req->bio->bio_cmd != BIO_SCSI)
                delete req;

            _req_free++;
            biodone(bio, response == SCSI_OK);

            barrier();
            s->cmp_cons_idx++;

            _waiter.wake();
        }
    }
}

int pvscsi::make_request(struct bio* bio)
{
    WITH_LOCK(_lock) {
        if (!bio)
            return EIO;

        struct pvscsi_priv *prv;
        u16 target = 0, lun = 0;
        if (bio->bio_cmd != BIO_SCSI) {
            prv = pvscsi::get_priv(bio);
            target = prv->target;
            lun = prv->lun;
        }

        return handle_bio(target, lun, bio);
    }
}

void pvscsi::add_lun(u16 target, u16 lun)
{
    struct pvscsi_priv* prv;
    struct device *dev;
    size_t devsize;

    if (!test_lun(target, lun))
        return;

    exec_read_capacity(target, lun, devsize);

    std::string dev_name("vblk");
    dev_name += std::to_string(_disk_idx++);
    dev = device_create(&pvscsi_driver, dev_name.c_str(), D_BLK);
    prv = static_cast<struct pvscsi_priv*>(dev->private_data);
    prv->strategy = pvscsi_strategy;
    prv->drv = this;
    prv->target = target;
    prv->lun = lun;
    dev->size = devsize;
    dev->max_io_size = config.max_sectors * SCSI_SECTOR_SIZE;
    read_partition_table(dev);

    printf("vmw-pvscsi: Add pvscsi device target=%d, lun=%-3d as %s, devsize=%lld\n", target, lun, dev_name.c_str(), devsize);
}

hw_driver* pvscsi::probe(hw_device* hw_dev)
{
    if (auto pci_dev = dynamic_cast<pci::device*>(hw_dev)) {
        auto id = pci_dev->get_id();
        if (id == hw_device_id(PVSCSI_VENDOR_ID_VMW, PVSCSI_DEVICE_ID_VMW)) {
            return new pvscsi(*pci_dev);
        }
    }
    return nullptr;
}

}
