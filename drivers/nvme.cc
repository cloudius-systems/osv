/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/cdefs.h>

#include "drivers/nvme.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>

#include <cassert>
#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include <osv/sched.hh>
#include <osv/trace.hh>
#include <osv/aligned_new.hh>

#include <osv/device.h>
#include <osv/bio.h>
#include <osv/ioctl.h>
#include <osv/contiguous_alloc.hh>
#include <osv/aligned_new.hh>

using namespace memory;

#include <sys/mman.h>
#include <sys/refcount.h>

#include <osv/drivers_config.h>

TRACEPOINT(trace_nvme_strategy, "bio=%p, bcount=%lu", struct bio*, size_t);

#define QEMU_VID 0x1b36

namespace nvme {

int driver::_disk_idx = 0;
int driver::_instance = 0;

struct nvme_priv {
    devop_strategy_t strategy;
    driver* drv;
    u32 nsid;
};

static void nvme_strategy(struct bio* bio) {
    auto* prv = reinterpret_cast<struct nvme_priv*>(bio->bio_dev->private_data);
    trace_nvme_strategy(bio, bio->bio_bcount);
    prv->drv->make_request(bio);
}

static int
nvme_read(struct device *dev, struct uio *uio, int io_flags)
{
    return bdev_read(dev, uio, io_flags);
}

static int
nvme_write(struct device *dev, struct uio *uio, int io_flags)
{
    return bdev_write(dev, uio, io_flags);
}

static int
nvme_open(struct device *dev, int ioflags)
{
    return 0;
}

static struct devops nvme_devops {
    nvme_open,
    no_close,
    nvme_read,
    nvme_write,
    no_ioctl,
    no_devctl,
    multiplex_strategy,
};

struct ::driver _driver = {
    "nvme",
    &nvme_devops,
    sizeof(struct nvme_priv),
};

static void setup_features_cmd(nvme_sq_entry_t* cmd, u8 feature_id, u32 val)
{
    memset(cmd, 0, sizeof(nvme_sq_entry_t));
    cmd->set_features.common.opc = NVME_ACMD_SET_FEATURES;
    cmd->set_features.fid = feature_id;
    cmd->set_features.val = val;
}

enum CMD_IDENTIFY_CNS {
    CMD_IDENTIFY_NAMESPACE = 0,
    CMD_IDENTIFY_CONTROLLER = 1,
};

#define NVME_NAMESPACE_DEFAULT_NS 1

static void setup_identify_cmd(nvme_sq_entry_t* cmd, u32 namespace_id, u32 cns)
{
    memset(cmd, 0, sizeof(nvme_sq_entry_t));
    cmd->identify.common.opc = NVME_ACMD_IDENTIFY;
    cmd->identify.common.nsid = namespace_id;
    cmd->identify.cns = cns;
}

driver::driver(pci::device &pci_dev)
     : _dev(pci_dev)
     , _msi(&pci_dev)
{
    auto parse_ok = parse_pci_config();
    assert(parse_ok);

    enable_msix();

    _id = _instance++;

    _doorbell_stride = 1 << (2 + _control_reg->cap.dstrd);

    //Wait for controller to become ready
    assert(wait_for_controller_ready_change(1) == 0);

    //Disable controller
    assert(enable_disable_controller(false) == 0);

    init_controller_config();

    create_admin_queue();

    //Enable controller
    assert(enable_disable_controller(true) == 0);

    assert(identify_controller() == 0);

    assert(identify_namespace(NVME_NAMESPACE_DEFAULT_NS) == 0);

    //Enable write cache if available
    if (_identify_controller->vwc & 0x1 && NVME_VWC_ENABLED) {
        enable_write_cache();
    }

    //Create IO queues
    create_io_queues();

    if (_identify_controller->vid != QEMU_VID) {
        set_interrupt_coalescing(20, 2);
    }

    std::string dev_name("vblk");
    dev_name += std::to_string(_disk_idx++);

    struct device* dev = device_create(&_driver, dev_name.c_str(), D_BLK);
    struct nvme_priv* prv = reinterpret_cast<struct nvme_priv*>(dev->private_data);

    unsigned int nsid = NVME_NAMESPACE_DEFAULT_NS;
    const auto& ns = _ns_data[nsid];
    off_t size = ((off_t) ns->blockcount) << ns->blockshift;

    prv->strategy = nvme_strategy;
    prv->drv = this;
    prv->nsid = nsid;
    dev->size = size;
    //IO size greater than 4096 << 9 would mean we need
    //more than 1 page for the prplist which is not implemented
    dev->max_io_size = mmu::page_size << ((9 < _identify_controller->mdts)? 9 : _identify_controller->mdts);

    read_partition_table(dev);

    debugf("nvme: Add device instances %d as %s, devsize=%lld, serial number:%s\n",
        _id, dev_name.c_str(), dev->size, _identify_controller->sn);
}

int driver::set_number_of_queues(u16 num, u16* ret)
{
    nvme_sq_entry_t cmd;
    setup_features_cmd(&cmd, NVME_FEATURE_NUM_QUEUES, (num << 16) | num);
    auto res = _admin_queue->submit_and_return_on_completion(&cmd);

    u16 cq_num = res.cs >> 16;
    u16 sq_num = res.cs & 0xffff;

    nvme_d("Queues supported: CQ num=%d, SQ num=%d, MSI/X entries=%d",
        res.cs >> 16, res.cs & 0xffff, _dev.msix_get_num_entries());

    if (res.sct != 0 || res.sc != 0)
        return EIO;

    if (num > cq_num || num > sq_num) {
        *ret = (cq_num > sq_num) ? cq_num : sq_num;
    } else {
        *ret = num;
    }
    return 0;
}

int driver::set_interrupt_coalescing(u8 threshold, u8 time)
{
    nvme_sq_entry_t cmd;
    setup_features_cmd(&cmd, NVME_FEATURE_INT_COALESCING, threshold | (time << 8));
    auto res = _admin_queue->submit_and_return_on_completion(&cmd);

    if (res.sct != 0 || res.sc != 0) {
        nvme_e("Failed to enable interrupt coalescing: sc=%#x sct=%#x", res.sc, res.sct);
        return EIO;
    } else {
        nvme_i("Enabled interrupt coalescing");
        return 0;
    }
}

void driver::enable_write_cache()
{
    nvme_sq_entry_t cmd;
    setup_features_cmd(&cmd, NVME_FEATURE_WRITE_CACHE, 1);
    auto res = _admin_queue->submit_and_return_on_completion(&cmd);
    if (res.sct != 0 || res.sc != 0) {
        nvme_e("Failed to enable write cache: sc=%#x sct=%#x", res.sc, res.sct);
    } else {
        nvme_i("Enabled write cache");
    }
}

void driver::create_io_queues()
{
    u16 ret;
    if (NVME_QUEUE_PER_CPU_ENABLED) {
        set_number_of_queues(sched::cpus.size(), &ret);
    } else {
        set_number_of_queues(1, &ret);
    }
    assert(ret >= 1);

    int qsize = (NVME_IO_QUEUE_SIZE < _control_reg->cap.mqes) ? NVME_IO_QUEUE_SIZE : _control_reg->cap.mqes + 1;
    if (NVME_QUEUE_PER_CPU_ENABLED) {
        for(sched::cpu* cpu : sched::cpus) {
            int qid = cpu->id + 1;
            create_io_queue(qid, qsize, cpu);
        }
    } else {
        create_io_queue(1, qsize);
    }
}

enum NVME_CONTROLLER_EN {
    CTRL_EN_DISABLE = 0,
    CTRL_EN_ENABLE = 1,
};

int driver::enable_disable_controller(bool enable)
{
    nvme_controller_config_t cc;
    cc.val = mmio_getl(&_control_reg->cc);

    u32 expected_en = enable ? CTRL_EN_DISABLE : CTRL_EN_ENABLE;
    u32 new_en = enable ? CTRL_EN_ENABLE : CTRL_EN_DISABLE;

    assert(cc.en == expected_en); //check current status
    cc.en = new_en;

    mmio_setl(&_control_reg->cc, cc.val);
    return wait_for_controller_ready_change(new_en);
}

int driver::wait_for_controller_ready_change(int ready)
{
    int timeout = mmio_getb(&_control_reg->cap.to) * 10000; // timeout in 0.05ms steps
    nvme_controller_status_t csts;
    for (int i = 0; i < timeout; i++) {
        csts.val = mmio_getl(&_control_reg->csts);
        if (csts.rdy == ready) return 0;
        usleep(50);
    }
    NVME_ERROR("timeout=%d waiting for ready %d", timeout, ready);
    return ETIME;
}

#define NVME_CTRL_CONFIG_IO_CQ_ENTRY_SIZE_16_BYTES 4
#define NVME_CTRL_CONFIG_IO_SQ_ENTRY_SIZE_64_BYTES 6
#define NVME_CTRL_CONFIG_PAGE_SIZE_4K              0

void driver::init_controller_config()
{
    nvme_controller_config_t cc;
    cc.val = mmio_getl(&_control_reg->cc.val);
    cc.iocqes = NVME_CTRL_CONFIG_IO_CQ_ENTRY_SIZE_16_BYTES;
    cc.iosqes = NVME_CTRL_CONFIG_IO_SQ_ENTRY_SIZE_64_BYTES;
    cc.mps = NVME_CTRL_CONFIG_PAGE_SIZE_4K;

    mmio_setl(&_control_reg->cc, cc.val);
}

void driver::create_admin_queue()
{
    u32* sq_doorbell = _control_reg->sq0tdbl;
    u32* cq_doorbell = (u32*) ((u64)sq_doorbell + _doorbell_stride);

    int qsize = NVME_ADMIN_QUEUE_SIZE;
    _admin_queue = std::unique_ptr<admin_queue_pair, aligned_new_deleter<admin_queue_pair>>(
        aligned_new<admin_queue_pair>(_id, 0, qsize, _dev, sq_doorbell, cq_doorbell, _ns_data));

    register_admin_interrupt();

    nvme_adminq_attr_t aqa;
    aqa.val = 0;
    aqa.asqs = aqa.acqs = qsize - 1;

    mmio_setl(&_control_reg->aqa, aqa.val);
    mmio_setq(&_control_reg->asq, _admin_queue->sq_phys_addr());
    mmio_setq(&_control_reg->acq, _admin_queue->cq_phys_addr());
}

template<typename Q>
void setup_create_io_queue_cmd(Q* create_queue_cmd, int qid, int qsize, u8 command_opcode, u64 queue_addr)
{
    assert(create_queue_cmd);
    memset(create_queue_cmd, 0, sizeof (*create_queue_cmd));

    create_queue_cmd->common.opc = command_opcode;
    create_queue_cmd->common.prp1 = queue_addr;
    create_queue_cmd->qid = qid;
    create_queue_cmd->qsize = qsize - 1;
    create_queue_cmd->pc = 1;
}

int driver::create_io_queue(int qid, int qsize, sched::cpu* cpu, int qprio)
{
    int iv = qid;

    u32* sq_doorbell = (u32*) ((u64) _control_reg->sq0tdbl + 2 * _doorbell_stride * qid);
    u32* cq_doorbell = (u32*) ((u64) sq_doorbell + _doorbell_stride);

    // create queue pair with allocated SQ and CQ ring buffers
    auto queue = std::unique_ptr<io_queue_pair, aligned_new_deleter<io_queue_pair>>(
        aligned_new<io_queue_pair>(_id, iv, qsize, _dev, sq_doorbell, cq_doorbell, _ns_data));

    // create completion queue command
    nvme_acmd_create_cq_t cmd_cq;
    setup_create_io_queue_cmd<nvme_acmd_create_cq_t>(
        &cmd_cq, qid, qsize, NVME_ACMD_CREATE_CQ, queue->cq_phys_addr());

    cmd_cq.iv = iv;
    cmd_cq.ien = 1;

    // create submission queue command
    nvme_acmd_create_sq_t cmd_sq;
    setup_create_io_queue_cmd<nvme_acmd_create_sq_t>(
        &cmd_sq, qid, qsize, NVME_ACMD_CREATE_SQ, queue->sq_phys_addr());

    cmd_sq.qprio = qprio;
    cmd_sq.cqid = qid;

    _io_queues.push_back(std::move(queue));

    register_io_interrupt(iv, qid - 1, cpu);

    //According to the NVMe spec, the completion queue (CQ) needs to be created before the submission queue (SQ)
    _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t*)&cmd_cq);
    _admin_queue->submit_and_return_on_completion((nvme_sq_entry_t*)&cmd_sq);

    debugf("nvme: Created I/O queue pair for qid:%d with size:%d\n", qid, qsize);

    return 0;
}

int driver::identify_controller()
{
    assert(_admin_queue);
    nvme_sq_entry_t cmd;
    setup_identify_cmd(&cmd, 0, CMD_IDENTIFY_CONTROLLER);
    auto data = new nvme_identify_ctlr_t;
    auto res = _admin_queue->submit_and_return_on_completion(&cmd, (void*) mmu::virt_to_phys(data), mmu::page_size);

    if (res.sc != 0 || res.sct != 0) {
        NVME_ERROR("Identify controller failed nvme%d, sct=%d, sc=%d", _id, res.sct, res.sc);
        return EIO;
    }

    _identify_controller.reset(data);
    return 0;
}

int driver::identify_namespace(u32 nsid)
{
    assert(_admin_queue);
    nvme_sq_entry_t cmd;
    setup_identify_cmd(&cmd, nsid, CMD_IDENTIFY_NAMESPACE);
    auto data = std::unique_ptr<nvme_identify_ns_t>(new nvme_identify_ns_t);
    auto res = _admin_queue->submit_and_return_on_completion(&cmd, (void*) mmu::virt_to_phys(data.get()), mmu::page_size);
    if (res.sc != 0 || res.sct != 0) {
        NVME_ERROR("Identify namespace failed nvme%d nsid=%d, sct=%d, sc=%d", _id, nsid, res.sct, res.sc);
        return EIO;
    }

    _ns_data.insert(std::make_pair(nsid, new nvme_ns_t));
    _ns_data[nsid]->blockcount = data->ncap;
    _ns_data[nsid]->blockshift = data->lbaf[data->flbas & 0xF].lbads;
    _ns_data[nsid]->blocksize = 1 << _ns_data[nsid]->blockshift;
    _ns_data[nsid]->bpshift = NVME_PAGESHIFT - _ns_data[nsid]->blockshift;
    _ns_data[nsid]->id = nsid;

    nvme_i("Identified namespace with nsid=%d, blockcount=%d, blocksize=%d",
        nsid, _ns_data[nsid]->blockcount, _ns_data[nsid]->blocksize);
    return 0;
}

int driver::make_request(bio* bio, u32 nsid)
{
    if (bio->bio_bcount % _ns_data[nsid]->blocksize || bio->bio_offset % _ns_data[nsid]->blocksize) {
        NVME_ERROR("bio request not block-aligned length=%d, offset=%d blocksize=%d\n",bio->bio_bcount, bio->bio_offset, _ns_data[nsid]->blocksize);
        return EINVAL;
    }
    bio->bio_offset = bio->bio_offset >> _ns_data[nsid]->blockshift;
    bio->bio_bcount = bio->bio_bcount >> _ns_data[nsid]->blockshift;

    assert((bio->bio_offset + bio->bio_bcount) <= _ns_data[nsid]->blockcount);

    if (bio->bio_cmd == BIO_FLUSH && (_identify_controller->vwc == 0 || !NVME_VWC_ENABLED )) {
        biodone(bio, true);
        return 0;
    }

    unsigned int qidx = sched::current_cpu->id % _io_queues.size();
    return _io_queues[qidx]->make_request(bio, nsid);
}

void driver::register_admin_interrupt()
{
    sched::thread* aq_thread = sched::thread::make([this] { this->_admin_queue->req_done(); },
        sched::thread::attr().name("nvme" + std::to_string(_id) + "_aq_req_done"));
    aq_thread->start();

    assert(msix_register(0, [this] { this->_admin_queue->disable_interrupts(); }, aq_thread));
}

void driver::enable_msix()
{
    _dev.set_bus_master(true);
    _dev.msix_enable();
    assert(_dev.is_msix());

    unsigned int vectors_num = 1; //at least for admin
    if (NVME_QUEUE_PER_CPU_ENABLED) {
        vectors_num += sched::cpus.size();
    } else {
        vectors_num += 1;
    }

    assert(vectors_num <= _dev.msix_get_num_entries());
    _msix_vectors = std::vector<std::unique_ptr<msix_vector>>(vectors_num);
}

bool driver::msix_register(unsigned iv,
    // high priority ISR
    std::function<void ()> isr,
    // bottom half
    sched::thread *t,
    bool assign_affinity)
{
    //Mask all interrupts...
    _dev.msix_mask_all();
    _dev.msix_mask_entry(iv);

    auto vec = std::unique_ptr<msix_vector>(new msix_vector(&_dev));
    _msi.assign_isr(vec.get(),
        [=]() mutable {
                  isr();
                  t->wake_with_irq_disabled();
              });

    if (!_msi.setup_entry(iv, vec.get())) {
        return false;
    }

    if (assign_affinity && t) {
        vec->set_affinity(t->get_cpu()->arch.apic_id);
    }

    if (iv < _msix_vectors.size()) {
        _msix_vectors[iv] = std::move(vec);
    } else {
        NVME_ERROR("binding_entry %d registration failed\n",iv);
        return false;
    }
    _msix_vectors[iv]->msix_unmask_entries();

    _dev.msix_unmask_all();
    _dev.msix_unmask_entry(iv);
    return true;
}

//qid should be the index that corresponds to the queue in _io_queues.
//In general qid = iv - 1
bool driver::register_io_interrupt(unsigned int iv, unsigned int qid, sched::cpu* cpu)
{
    sched::thread* t;
    bool ok;

    if (_io_queues.size() <= qid) {
        NVME_ERROR("queue %d not initialized\n",qid);
        return false;
    }

    if (_io_queues[qid]->_id != iv)
        nvme_w("Queue %d ->_id = %d != iv %d\n", qid, _io_queues[qid]->_id, iv);

    t = sched::thread::make([this,qid] { this->_io_queues[qid]->req_done(); },
        sched::thread::attr().name("nvme" + std::to_string(_id) + "_ioq" + std::to_string(qid) + "_iv" + std::to_string(iv)));
    t->start();

    // If cpu specified, let us pin the worker thread to this cpu
    bool pin = cpu != nullptr;
    if (pin) {
        sched::thread::pin(t, cpu);
    }

    ok = msix_register(iv, [this,qid] { this->_io_queues[qid]->disable_interrupts(); }, t, pin);
    if (not ok)
        NVME_ERROR("Interrupt registration failed: queue=%d interruptvector=%d\n", qid, iv);
    return ok;
}

void driver::dump_config(void)
{
    u8 B, D, F;
    _dev.get_bdf(B, D, F);

    _dev.dump_config();
    nvme_d("%s [%x:%x.%x] vid:id= %x:%x", get_name().c_str(),
             (u16)B, (u16)D, (u16)F,
             _dev.get_vendor_id(),
             _dev.get_device_id());
}

bool driver::parse_pci_config()
{
    _bar0 = _dev.get_bar(1);
    if (_bar0 == nullptr) {
        return false;
    }
    _bar0->map();
    if (!_bar0->is_mapped()) {
        return false;
    }
    _control_reg = (nvme_controller_reg_t*) _bar0->get_mmio();
    return true;
}

hw_driver* driver::probe(hw_device* dev)
{
    if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
        if ((pci_dev->get_base_class_code() == pci::function::PCI_CLASS_STORAGE) &&
            (pci_dev->get_sub_class_code() == pci::function::PCI_SUB_CLASS_STORAGE_NVMC) &&
            (pci_dev->get_programming_interface() == 2))// detect NVMe device
            return aligned_new<driver>(*pci_dev);
    }
    return nullptr;
}

}
