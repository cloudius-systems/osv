/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/drivers_config.h>
#include <string>

#include <osv/debug.h>
#include <osv/device.h>
#include <osv/interrupt.hh>
#include <osv/mmio.hh>
#include <osv/msi.hh>
#include <osv/sched.hh>

#include "drivers/pci-device.hh"
#include "drivers/virtio.hh"
#include "drivers/virtio-fs.hh"
#include "drivers/virtio-vring.hh"
#include "drivers/blk-common.hh"
#include "fs/virtiofs/fuse_kernel.h"

using fuse_request = virtio::fs::fuse_request;

namespace virtio {

static void fuse_req_enqueue_input(vring& queue, fuse_request& req)
{
    // Header goes first
    queue.add_out_sg(&req.in_header, sizeof(struct fuse_in_header));

    // Add fuse in arguments as out sg
    if (req.input_args_size > 0) {
        queue.add_out_sg(req.input_args_data, req.input_args_size);
    }
}

static void fuse_req_enqueue_output(vring& queue, fuse_request& req)
{
    // Header goes first
    queue.add_in_sg(&req.out_header, sizeof(struct fuse_out_header));

    // Add fuse out arguments as in sg
    if (req.output_args_size > 0) {
        queue.add_in_sg(req.output_args_data, req.output_args_size);
    }
}

int fs::_instance = 0;

static struct devops fs_devops {
    no_open,
    no_close,
    no_read,
    no_write,
    blk_ioctl,
    no_devctl,
    no_strategy,
};

struct driver fs_driver = {
    "virtio_fs",
    &fs_devops,
    sizeof(fs*),
};

bool fs::ack_irq()
{
    auto isr = _dev.read_and_ack_isr();
    auto* queue = get_virt_queue(VQ_REQUEST);

    if (isr) {
        queue->disable_interrupts();
        return true;
    }
    return false;
}

fs::fs(virtio_device& virtio_dev)
    : virtio_driver(virtio_dev), _map_align(-1)
{
    _driver_name = "virtio-fs";
    _id = _instance++;
    virtio_i("VIRTIO FS INSTANCE %d\n", _id);

    // Steps 4, 5 & 6 - negotiate and confirm features
    setup_features();
    read_config();
    if (_config.num_queues < 1) {
        virtio_i("Expected at least one request queue -> baling out!\n");
        return;
    }

    // Step 7 - generic init of virtqueues
    probe_virt_queues();

    // register the single irq callback for the block
    sched::thread* t = sched::thread::make([this] { this->req_done(); },
        sched::thread::attr().name("virtio-fs"));
    t->start();
    auto* queue = get_virt_queue(VQ_REQUEST);

    interrupt_factory int_factory;
#if CONF_drivers_pci
    int_factory.register_msi_bindings = [queue, t](interrupt_manager& msi) {
        msi.easy_register({
            {VQ_HIPRIO, nullptr, nullptr},
            {VQ_REQUEST, [=] { queue->disable_interrupts(); }, t}
        });
    };

    int_factory.create_pci_interrupt = [this, t](pci::device& pci_dev) {
        return new pci_interrupt(
            pci_dev,
            [=] { return this->ack_irq(); },
            [=] { t->wake_with_irq_disabled(); });
    };
#endif

#ifdef __x86_64__
#if CONF_drivers_mmio
    int_factory.create_gsi_edge_interrupt = [this, t]() {
        return new gsi_edge_interrupt(
            _dev.get_irq(),
            [=] { if (this->ack_irq()) t->wake_with_irq_disabled(); });
    };
#endif
#endif

    _dev.register_interrupt(int_factory);

    // Enable indirect descriptor
    queue->set_use_indirect(true);

    // Step 8
    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    // TODO: Don't ignore the virtio-fs tag and use that instead of _id for
    // identifying the device (e.g. something like /dev/virtiofs/<tag> or at
    // least /dev/virtiofs-<tag> would be nice, but devfs does not support
    // nested directories or device names > 12). Linux does not create a devfs
    // entry and instead uses the virtio-fs tag passed to mount directly.
    std::string dev_name("virtiofs");
    dev_name += std::to_string(_id);
    struct device* dev = device_create(&fs_driver, dev_name.c_str(), D_BLK);
    dev->private_data = this;
    debugf("virtio-fs: Add device instance %d as [%s]\n", _id,
        dev_name.c_str());
}

fs::~fs()
{
    // TODO: In theory maintain the list of free instances and gc it
    // including the thread objects and their stack
}

void fs::read_config()
{
    virtio_conf_read(0, &_config, sizeof(_config));
    debugf("virtio-fs: Detected device with tag: [%s] and num_queues: %d\n",
        _config.tag, _config.num_queues);

    // Query for DAX window
    mmioaddr_t dax_addr;
    u64 dax_len;
    if (_dev.get_shm(0, dax_addr, dax_len)) {
        _dax.addr = dax_addr;
        _dax.len = dax_len;
        debugf("virtio-fs: Detected DAX window with length %lld\n", dax_len);
    } else {
        _dax.addr = mmio_nullptr;
        _dax.len = 0;
    }
}

void fs::req_done()
{
    auto* queue = get_virt_queue(VQ_REQUEST);

    while (true) {
        wait_for_queue(queue, &vring::used_ring_not_empty);

        fuse_request* req;
        u32 len;
        while ((req = static_cast<fuse_request*>(queue->get_buf_elem(&len))) !=
            nullptr) {

            req->done();
            queue->get_buf_finalize();
        }

        // wake up the requesting thread in case the ring was full before
        queue->wakeup_waiter();
    }
}

int fs::make_request(fuse_request& req)
{
    auto* queue = get_virt_queue(VQ_REQUEST);

    WITH_LOCK(_lock) {
        queue->init_sg();

        fuse_req_enqueue_input(*queue, req);
        fuse_req_enqueue_output(*queue, req);

        queue->add_buf_wait(&req);
        queue->kick();
    }

    return 0;
}

u64 fs::get_driver_features()
{
    auto base = virtio_driver::get_driver_features();
    return base | ((u64)1 << VIRTIO_F_VERSION_1);
}

hw_driver* fs::probe(hw_device* dev)
{
    return virtio::probe<fs, VIRTIO_ID_FS>(dev);
}

}
