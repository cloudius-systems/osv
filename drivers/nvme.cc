#include <sys/cdefs.h>

#include "drivers/nvme.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>

#include <cassert>
#include <sstream>
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

using namespace memory;

#include <sys/mman.h>
#include <sys/refcount.h>

#include <osv/drivers_config.h>
#include "drivers/io-test.hh"

TRACEPOINT(trace_nvme_read_config, "capacity=%lu blk_size=%u max_io_size=%u", u64, u32, u64);
TRACEPOINT(trace_nvme_strategy, "bio=%p", struct bio*);
TRACEPOINT(trace_nvme_vwc_enabled, "sc=%#x sct=%#x", u16, u16);
TRACEPOINT(trace_nvme_number_of_queues, "cq num=%d, sq num=%d, iv_num=%d", u16, u16, u32);
TRACEPOINT(trace_nvme_identify_namespace, "nsid=%d, blockcount=%d, blocksize=%d", u32, u64, u16);
TRACEPOINT(trace_nvme_register_interrupt, "_io_queues[%d], iv=%d", int, int);


#define QEMU_VID 0x1b36

std::unique_ptr<nvme_sq_entry_t> alloc_cmd() {
    auto cmd = std::unique_ptr<nvme_sq_entry_t>(new nvme_sq_entry_t);
    assert(cmd);
    memset(cmd.get(), 0, sizeof(nvme_ns_t));
    return cmd;
}

struct nvme_priv {
    devop_strategy_t strategy;
    nvme* drv;
    u32 nsid;
};

static void nvme_strategy(struct bio* bio) {
    auto* prv = reinterpret_cast<struct nvme_priv*>(bio->bio_dev->private_data);
    trace_nvme_strategy(bio);
    prv->drv->make_request(bio);
}

static int
nvme_read(struct device *dev, struct uio *uio, int ioflags)
{
  return bdev_read(dev, uio, ioflags);
}

static int
nvme_write(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_write(dev, uio, ioflags);
}

static int
nvme_direct_rw(struct device *dev, struct uio *uio, int ioflags)
{
    auto* prv = reinterpret_cast<struct nvme_priv*>(dev->private_data);

	assert((uio->uio_offset % prv->drv->_ns_data[prv->nsid]->blocksize) == 0);
	assert((uio->uio_resid % prv->drv->_ns_data[prv->nsid]->blocksize) == 0);

    bio* complete_io = alloc_bio();

    u8 opcode;
    switch (uio->uio_rw) {
    case UIO_READ :
        opcode = BIO_READ;
        break;
    case UIO_WRITE :
        opcode = BIO_WRITE;
        break;
    default :
        return EINVAL;
    }

    refcount_init(&complete_io->bio_refcnt, uio->uio_iovcnt);

    while(uio->uio_iovcnt > 0) 
    {
        bio* bio = alloc_bio();
        bio->bio_cmd = opcode;
        bio->bio_dev = dev;

        assert((uio->uio_iov->iov_len % prv->drv->_ns_data[prv->nsid]->blocksize) == 0);

        bio->bio_bcount = uio->uio_iov->iov_len;
        bio->bio_data = uio->uio_iov->iov_base;
        bio->bio_offset = uio->uio_offset;

        bio->bio_caller1 = complete_io;
        bio->bio_private = complete_io->bio_private;
        bio->bio_done = multiplex_bio_done;
        
        dev->driver->devops->strategy(bio);

        uio->uio_offset += uio->uio_iov->iov_len;
        uio->uio_resid -= uio->uio_iov->iov_len;
        uio->uio_iov++;
        uio->uio_iovcnt--;
    }
    assert(uio->uio_resid == 0);
    int ret = bio_wait(complete_io);
    destroy_bio(complete_io);

    return ret;
}

static int
nvme_open(struct device *dev, int ioflags)
{
    return 0;
}

#include "drivers/blk_ioctl.hh"

static struct devops nvme_devops {
    nvme_open,
    no_close,
    NVME_DIRECT_RW_ENABLED ? nvme_direct_rw : nvme_read,
    NVME_DIRECT_RW_ENABLED ? nvme_direct_rw : nvme_write,
    blk_ioctl,
    no_devctl,
    multiplex_strategy,
};

struct driver nvme_driver = {
    "nvme",
    &nvme_devops,
    sizeof(struct nvme_priv),
};

int nvme::_instance = 0;

extern std::vector<sched::cpu*> sched::cpus;

nvme::nvme(pci::device &dev)
     : _dev(dev)
     , _msi(&dev)
{
    parse_pci_config();
    u16 command = dev.get_command();
    command |= 0x4 | 0x2 | 0x400;
    dev.set_command(command);

    _id = _instance++;
    
    _doorbellstride = 1 << (2 + _control_reg->cap.dstrd);
    
    wait_for_controller_ready_change(1);
    disable_controller();

    init_controller_config();

    create_admin_queue();
    
    enable_controller();

    identify_controller();

    if(NVME_CHECK_FOR_ADDITIONAL_NAMESPACES) {
        identify_active_namespaces(1);
    } else {
        identify_namespace(1);
    }

    if(_identify_controller->vwc & 0x1 && NVME_VWC_ENABLED) {
        auto cmd = alloc_cmd();
        cmd->set_features.common.opc = NVME_ACMD_SET_FEATURES;
        cmd->set_features.fid = NVME_FEATURE_WRITE_CACHE;
        cmd->set_features.val = 1;
        auto res = _admin_queue->submit_and_return_on_completion(std::move(cmd));
        trace_nvme_vwc_enabled(res->sc,res->sct);
    }

    if(NVME_QUEUE_PER_CPU_ENABLED) {
        u16 num = sched::cpus.size();
        u16 ret;
        set_number_of_queues(num, &ret);   
        create_io_queues_foreach_cpu();
    }else {
        u16 ret;
        set_number_of_queues(1, &ret);
        assert(ret>=1);
        create_io_queue();
    }

    if(_identify_controller->vid != QEMU_VID) {
        set_interrupt_coalescing(20,2);
    }

    struct nvme_priv* prv;
    struct device *osv_dev;
    
    debugf("nvme: %s\n", _identify_controller->sn);
    
    for(const auto& ns : _ns_data) {
        std::string dev_name;
        if(ns.first == 1 && _id == 0) {
            dev_name = "vblk";
            dev_name += std::to_string(_id);
        } else {
            dev_name = "nvme";
            dev_name += std::to_string(_id) + "n";
            dev_name += std::to_string(ns.first);
        }
        off_t size = ((off_t) ns.second->blockcount) << ns.second->blockshift;
        
        debugf("nvme: Add namespace %d of nvme device %d as %s, devsize=%lld\n", ns.first, _id, dev_name.c_str(), size);

        osv_dev = device_create(&nvme_driver,dev_name.c_str(), D_BLK);
        prv = reinterpret_cast<struct nvme_priv*>(osv_dev->private_data);
        prv->strategy = nvme_strategy;
        prv->drv = this;
        prv->nsid = ns.first;
        osv_dev->size = size;
        /*
        * IO size greater than 4096 << 9 would mean we need 
        * more than 1 page for the prplist which is not implemented
        */
        osv_dev->max_io_size = 4096 << ((9 < _identify_controller->mdts)? 9 : _identify_controller->mdts );

        #if CONF_drivers_io_test
            test_block_device(osv_dev, 20*1e6, 8);
            test_block_device(osv_dev, 20*1e6, 512);
        #endif
        
        read_partition_table(osv_dev);
    }
}

int nvme::set_number_of_queues(u16 num, u16* ret) 
{
    auto cmd = alloc_cmd();
    cmd->set_features.common.opc = NVME_ACMD_SET_FEATURES;
    cmd->set_features.fid = NVME_FEATURE_NUM_QUEUES;
    cmd->set_features.val = (num << 16) | num;
    std::unique_ptr<nvme_cq_entry_t> res = _admin_queue->submit_and_return_on_completion(std::move(cmd));
    u16 cq_num, sq_num;
    cq_num = res->cs >> 16;
    sq_num = res->cs & 0xffff;
    
    trace_nvme_number_of_queues(res->cs >> 16, res->cs & 0xffff,_dev.msix_get_num_entries());
    
    if(res->sct != 0 || res->sc != 0)
        return EIO;

    if(num > cq_num || num > sq_num) {
        *ret = (cq_num > sq_num) ? cq_num : sq_num;  
    } else {
        *ret = num;
    }
    return 0;
}
/*time in 100ms increments*/
int nvme::set_interrupt_coalescing(u8 threshold, u8 time) 
{
    auto cmd = alloc_cmd();
    cmd->set_features.common.opc = NVME_ACMD_SET_FEATURES;
    cmd->set_features.fid = NVME_FEATURE_INT_COALESCING;
    cmd->set_features.val = threshold | (time << 8);
    std::unique_ptr<nvme_cq_entry_t> res = _admin_queue->submit_and_return_on_completion(std::move(cmd));

    if(res->sct != 0 || res->sc != 0)
        return EIO;
    return 0;
}

void nvme::enable_controller() 
{
    nvme_controller_config_t cc;
    cc.val = mmio_getl(&_control_reg->cc);
    
    assert(cc.en == 0);
    cc.en = 1;

    mmio_setl(&_control_reg->cc,cc.val);
    int s = wait_for_controller_ready_change(1);
    assert(s==0);
}

void nvme::disable_controller() 
{   
    nvme_controller_config_t cc;
    cc.val = mmio_getl(&_control_reg->cc);
    
    assert(cc.en == 1);
    cc.en = 0;

    mmio_setl(&_control_reg->cc,cc.val);
    int s = wait_for_controller_ready_change(0);
    assert(s==0);
}

int nvme::wait_for_controller_ready_change(int ready)
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

void nvme::init_controller_config()
{
    nvme_controller_config_t cc;
    cc.val = mmio_getl(&_control_reg->cc.val);
    cc.iocqes    = 4;  // completion queue entry size 16B
    cc.iosqes    = 6;  // submission queue entry size 64B
    cc.mps        = 0;  // memory page size 4096B

    mmio_setl(&_control_reg->cc, cc.val);
}

void nvme::create_admin_queue()
{
    int qsize = NVME_ADMIN_QUEUE_SIZE;
    nvme_sq_entry_t* sqbuf = (nvme_sq_entry_t*) alloc_phys_contiguous_aligned(qsize * sizeof(nvme_sq_entry_t),4096);
    nvme_cq_entry_t* cqbuf = (nvme_cq_entry_t*) alloc_phys_contiguous_aligned(qsize * sizeof(nvme_cq_entry_t),4096);
    
    nvme_adminq_attr_t aqa;
    aqa.val = 0;
    aqa.asqs = aqa.acqs = qsize - 1;
    
    u32* sq_doorbell = _control_reg->sq0tdbl;
    u32* cq_doorbell = (u32*) ((u64)sq_doorbell + _doorbellstride);

    _admin_queue = std::unique_ptr<nvme_admin_queue_pair>(new nvme_admin_queue_pair(_id,0, qsize, _dev, sqbuf, sq_doorbell, cqbuf, cq_doorbell, _ns_data));
    
    register_admin_interrupts();
    
    mmio_setl(&_control_reg->aqa, aqa.val);
    mmio_setq(&_control_reg->asq, (u64) mmu::virt_to_phys((void*) sqbuf));
    mmio_setq(&_control_reg->acq, (u64) mmu::virt_to_phys((void*) cqbuf));
}

int nvme::create_io_queue(int qsize, int qprio) 
{
    u32* sq_doorbell;
    u32* cq_doorbell;
    int id = _io_queues.size() + 1;
    int iv = id;
    qsize = (qsize < _control_reg->cap.mqes) ? qsize : _control_reg->cap.mqes + 1;

    nvme_sq_entry_t* sqbuf = (nvme_sq_entry_t*) alloc_phys_contiguous_aligned(qsize * sizeof(nvme_sq_entry_t),4096);
    nvme_cq_entry_t* cqbuf = (nvme_cq_entry_t*) alloc_phys_contiguous_aligned(qsize * sizeof(nvme_cq_entry_t),4096);
    assert(sqbuf);
    assert(cqbuf);
    memset(sqbuf,0,sizeof(nvme_sq_entry_t)*qsize);
    memset(cqbuf,0,sizeof(nvme_cq_entry_t)*qsize);

    // create completion queue
    nvme_acmd_create_cq_t* cmd = (nvme_acmd_create_cq_t*) malloc(sizeof(nvme_acmd_create_cq_t));
    assert(cmd);
    memset(cmd, 0, sizeof (*cmd));
    
    cmd->qid = id;
    cmd->qsize = qsize - 1;
    cmd->iv = iv;
    cmd->pc = 1;
    cmd->ien = 1;
    cmd->common.opc = NVME_ACMD_CREATE_CQ;
    cmd->common.prp1 = (u64) mmu::virt_to_phys(cqbuf);

    // create submission queue
    nvme_acmd_create_sq_t* cmd_sq = (nvme_acmd_create_sq_t*) malloc(sizeof(nvme_acmd_create_sq_t));
    assert(cmd_sq);
    memset(cmd_sq, 0, sizeof(nvme_acmd_create_sq_t));
    
    cmd_sq->pc = 1;
    cmd_sq->qprio = qprio; // 0=urgent 1=high 2=medium 3=low
    cmd_sq->qid = id;
    cmd_sq->cqid = id;
    cmd_sq->qsize = qsize - 1;
    cmd_sq->common.opc = NVME_ACMD_CREATE_SQ;
    cmd_sq->common.prp1 = (u64) mmu::virt_to_phys(sqbuf);

    sq_doorbell = (u32*) ((u64) _control_reg->sq0tdbl + 2 * _doorbellstride * id);
    cq_doorbell = (u32*) ((u64) sq_doorbell + _doorbellstride);

    _io_queues.push_back(std::unique_ptr<nvme_io_queue_pair>(new nvme_io_queue_pair(_id, iv, qsize, _dev, sqbuf, sq_doorbell, cqbuf, cq_doorbell, _ns_data)));
    
    register_interrupt(iv,id-1);

    _admin_queue->submit_and_return_on_completion(std::unique_ptr<nvme_sq_entry_t>((nvme_sq_entry_t*)cmd));
    _admin_queue->submit_and_return_on_completion(std::unique_ptr<nvme_sq_entry_t>((nvme_sq_entry_t*)cmd_sq));

    return id -1;
}

void nvme::create_io_queues_foreach_cpu()
{
    int iv,id;
    int qsize = NVME_IO_QUEUE_SIZE;
    
    assert(_io_queues.size()==0);

    u32* sq_doorbell;
    u32* cq_doorbell;

    for(sched::cpu* cpu : sched::cpus) {
        id = cpu->id;
        iv = id + 1;
        nvme_sq_entry_t* sqbuf = (nvme_sq_entry_t*) alloc_phys_contiguous_aligned(qsize * sizeof(nvme_sq_entry_t),4096);
        nvme_cq_entry_t* cqbuf = (nvme_cq_entry_t*) alloc_phys_contiguous_aligned(qsize * sizeof(nvme_cq_entry_t),4096);
        assert(sqbuf);
        assert(cqbuf);
        memset(sqbuf,0,sizeof(nvme_sq_entry_t)*qsize);
        memset(cqbuf,0,sizeof(nvme_cq_entry_t)*qsize);

        nvme_acmd_create_cq_t* cmd = (nvme_acmd_create_cq_t*) malloc(sizeof(nvme_acmd_create_cq_t));
        assert(cmd);
        memset(cmd, 0, sizeof (*cmd));

        cmd->qid = iv;
        cmd->qsize = qsize - 1;
        cmd->iv = iv;
        cmd->pc = 1;
        cmd->ien = 1;
        cmd->common.opc = NVME_ACMD_CREATE_CQ;
        cmd->common.prp1 = (u64) mmu::virt_to_phys(cqbuf);

        // create submission queue
        nvme_acmd_create_sq_t* cmd_sq = (nvme_acmd_create_sq_t*) malloc(sizeof(nvme_acmd_create_sq_t));
        assert(cmd_sq);
        memset(cmd_sq, 0, sizeof(nvme_acmd_create_sq_t));

        cmd_sq->pc = 1;
        cmd_sq->qprio = 2; // 0=urgent 1=high 2=medium 3=low
        cmd_sq->qid = iv;
        cmd_sq->cqid = iv;
        cmd_sq->qsize = qsize - 1;
        cmd_sq->common.opc = NVME_ACMD_CREATE_SQ;
        cmd_sq->common.prp1 = (u64) mmu::virt_to_phys(sqbuf);

        sq_doorbell = (u32*) ((u64) _control_reg->sq0tdbl + 2 * _doorbellstride * iv);
        cq_doorbell = (u32*) ((u64) sq_doorbell + _doorbellstride);

        _io_queues.push_back(std::unique_ptr<nvme_io_queue_pair>(new nvme_io_queue_pair(_id, iv, qsize, _dev, sqbuf, sq_doorbell, cqbuf, cq_doorbell, _ns_data)));

        register_interrupt(iv,id,true,cpu);

        _admin_queue->submit_and_return_on_completion(std::unique_ptr<nvme_sq_entry_t>((nvme_sq_entry_t*)cmd));
        _admin_queue->submit_and_return_on_completion(std::unique_ptr<nvme_sq_entry_t>((nvme_sq_entry_t*)cmd_sq));
    }
}

int nvme::identify_controller()
{
    assert(_admin_queue);
    auto cmd = alloc_cmd();
    cmd->identify.cns = 1;
    cmd->identify.common.opc = NVME_ACMD_IDENTIFY;
    auto data = new nvme_identify_ctlr_t;
    auto res = _admin_queue->submit_and_return_on_completion(std::move(cmd), (void*) mmu::virt_to_phys(data),4096);
    
    if(res->sc != 0 || res->sct != 0) {
        NVME_ERROR("Identify controller failed nvme%d, sct=%d, sc=%d", _id, res->sct, res->sc);
        return EIO;
    }

    _identify_controller.reset(data);
    return 0;
}

int nvme::identify_namespace(u32 ns)
{
    assert(_admin_queue);
    auto cmd = alloc_cmd();
    cmd->identify.cns = 0;
    cmd->identify.common.nsid = ns;
    cmd->identify.common.opc = NVME_ACMD_IDENTIFY;
    auto data = std::unique_ptr<nvme_identify_ns_t>(new nvme_identify_ns_t);
    
    auto res = _admin_queue->submit_and_return_on_completion(std::move(cmd), (void*) mmu::virt_to_phys(data.get()),4096);
    if(res->sc != 0 || res->sct != 0) {
        NVME_ERROR("Identify namespace failed nvme%d nsid=%d, sct=%d, sc=%d", _id, ns, res->sct, res->sc);
        return EIO;
    }

    _ns_data.insert(std::make_pair(ns, new nvme_ns_t));
    _ns_data[ns]->blockcount = data->ncap;
    _ns_data[ns]->blockshift = data->lbaf[data->flbas & 0xF].lbads;
    _ns_data[ns]->blocksize = 1 << _ns_data[ns]->blockshift;
    _ns_data[ns]->bpshift = NVME_PAGESHIFT - _ns_data[ns]->blockshift;
    _ns_data[ns]->id = ns;
    
    trace_nvme_identify_namespace(ns, _ns_data[ns]->blockcount, _ns_data[ns]->blocksize);
    return 0;
}

//identify all active namespaces with nsid >= start
int nvme::identify_active_namespaces(u32 start)
{
    assert(start >= 1);
    assert(_identify_controller);
    //max number of namespaces supported by the controller
    u32 nn = _identify_controller->nn;
    assert(nn > start);

    auto cmd = alloc_cmd();
    cmd->identify.cns = 2;
    cmd->identify.common.nsid = start - 1;
    cmd->identify.common.opc = NVME_ACMD_IDENTIFY;
    auto active_namespaces = (u64*) alloc_phys_contiguous_aligned(4096, 4);
    memset(active_namespaces, 0, 4096);

    _admin_queue->submit_and_return_on_completion(std::move(cmd), (void*) mmu::virt_to_phys(active_namespaces), 4096);
    int err;
    for(int i=0; i < 1024; i++) {
        if(active_namespaces[i]) {
            err = identify_namespace(active_namespaces[i]);
            if (err) {
                free_phys_contiguous_aligned(active_namespaces);
                return err;
            }
        } else { break;}
    }
    free_phys_contiguous_aligned(active_namespaces);
    return 0;
}

int nvme::make_request(bio* bio, u32 nsid)
{  
    if(bio->bio_bcount % _ns_data[nsid]->blocksize || bio->bio_offset % _ns_data[nsid]->blocksize) {
        NVME_ERROR("bio request not block-aligned length=%d, offset=%d blocksize=%d\n",bio->bio_bcount, bio->bio_offset, _ns_data[nsid]->blocksize);
        return EINVAL;
    }
    bio->bio_offset = bio->bio_offset >> _ns_data[nsid]->blockshift;
    bio->bio_bcount = bio->bio_bcount >> _ns_data[nsid]->blockshift;

    assert((bio->bio_offset + bio->bio_bcount) <= _ns_data[nsid]->blockcount);
    
    if(bio->bio_cmd == BIO_FLUSH && (_identify_controller->vwc == 0 || !NVME_VWC_ENABLED )) {
        biodone(bio,true);
        return 0;
    }

    if(sched::current_cpu->id >= _io_queues.size())
        return _io_queues[0]->make_request(bio, nsid);

    return _io_queues[sched::current_cpu->id]->make_request(bio, nsid);
}

void nvme::register_admin_interrupts() 
{
    sched::thread* aq_thread = sched::thread::make([this] { this->_admin_queue->req_done(); },
        sched::thread::attr().name("nvme"+ std::to_string(_id)+"_aq_req_done"));
    aq_thread->start();
        
    bool ok = msix_register(0, [this] { this->_admin_queue->disable_interrupts(); }, aq_thread);
    _dev.msix_unmask_entry(0);
    if(not ok)
        printf("admin interrupt registration failed\n");
}

bool nvme::msix_register(unsigned iv,
    // high priority ISR
    std::function<void ()> isr,
    // bottom half
    sched::thread *t,
    bool assign_affinity)
{
    // Enable the device msix capability,
    // masks all interrupts...
    if (_dev.is_msix()) {
        _dev.msix_enable();
    } else {
        return false;
    }
    _dev.msix_mask_all();

    if(_msix_vectors.empty())
        _msix_vectors = std::vector<std::unique_ptr<msix_vector>>(_dev.msix_get_num_entries());
    
    auto vec = std::unique_ptr<msix_vector>(new msix_vector(&_dev));
    bool assign_ok;
    _dev.msix_mask_entry(iv);
    if (t) {
        assign_ok =
            _msi.assign_isr(vec.get(),
                [=]() mutable {
                                isr();
                                t->wake_with_irq_disabled();
                              });
    } else {
        return false;
    }
    if (!assign_ok) {
        return false;
    }
    bool setup_ok = _msi.setup_entry(iv, vec.get());
    if (!setup_ok) {
        return false;
    }
    if (assign_affinity) {
        vec->set_affinity(t->get_cpu()->arch.apic_id);
    }

    if(iv < _msix_vectors.size()) {
        _msix_vectors.at(iv) = std::move(vec);
    } else {
        NVME_ERROR("binding_entry %d registration failed\n",iv);
        return false;
    }
    _msix_vectors.at(iv)->msix_unmask_entries();

    _dev.msix_unmask_all();
    return true;
}
//qid should be the index that corresponds to the queue in _io_queues.
//In general qid = iv - 1
bool nvme::register_interrupt(unsigned int iv, unsigned int qid, bool pin_t, sched::cpu* cpu)
{
    sched::thread* t;
    bool ok;

    if(_io_queues.size() <= qid) {
        NVME_ERROR("queue %d not initialized\n",qid);
        return false;
    }

    if(_io_queues[qid]->_id != iv)
        printf("Warning: Queue %d ->_id = %d != iv %d\n",qid,_io_queues[qid]->_id,iv);

    trace_nvme_register_interrupt(qid, iv);
    t = sched::thread::make([this,qid] { this->_io_queues[qid]->req_done(); },
        sched::thread::attr().name("nvme" + std::to_string(_id) + "_ioq" + std::to_string(qid) + "_iv" +std::to_string(iv)));
    t->start();
    if(pin_t && cpu) {
        sched::thread::pin(t,cpu);
    }

    ok = msix_register(iv, [this,qid] { this->_io_queues[qid]->disable_interrupts(); }, t,pin_t);
    _dev.msix_unmask_entry(iv);
    if(not ok)
        NVME_ERROR("Interrupt registration failed: queue=%d interruptvector=%d\n",qid,iv);
    return ok;
}

void nvme::dump_config(void)
{
    u8 B, D, F;
    _dev.get_bdf(B, D, F);

    _dev.dump_config();
    nvme_d("%s [%x:%x.%x] vid:id= %x:%x", get_name().c_str(),
             (u16)B, (u16)D, (u16)F,
             _dev.get_vendor_id(),
             _dev.get_device_id());
}

void nvme::parse_pci_config()
{
    _bar0 = _dev.get_bar(1);
    _bar0->map();
    if (_bar0 == nullptr) {
        throw std::runtime_error("BAR1 is absent");
    }
    assert(_bar0->is_mapped());
    _control_reg = (nvme_controller_reg_t*) _bar0->get_mmio();
}

hw_driver* nvme::probe(hw_device* dev)
{
    if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
        if ((pci_dev->get_base_class_code()==1) && (pci_dev->get_sub_class_code()==8) && (pci_dev->get_programming_interface()==2)) // detect NVMe device
            return aligned_new<nvme>(*pci_dev);
    }
    return nullptr;
}
