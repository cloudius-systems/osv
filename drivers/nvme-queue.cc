#include <sys/cdefs.h>

#include "nvme-queue.hh"
#include <osv/bio.h>
#include <vector>
#include <memory>
#include <osv/contiguous_alloc.hh>

extern std::unique_ptr<nvme_sq_entry_t> alloc_cmd();

TRACEPOINT(trace_nvme_io_queue_wake, "nvme%d qid=%d", int, int);
TRACEPOINT(trace_nvme_wait_for_completion_queue_entries, "nvme%d qid=%d,have_elements=%d", int, int, bool);
TRACEPOINT(trace_nvme_completion_queue_not_empty, "nvme%d qid=%d,not_empty=%d", int, int, bool);
TRACEPOINT(trace_nvme_enable_interrupts, "nvme%d qid=%d", int, int);
TRACEPOINT(trace_nvme_disable_interrupts, "nvme%d qid=%d", int, int);

TRACEPOINT(trace_nvme_read, "nvme%d qid=%d cid=%d, bio data=%#x, slba=%d, nlb=%d", int, int , u16, void*, u64, u32);
TRACEPOINT(trace_nvme_write, "nvme%d qid=%d cid=%d, bio data=%#x, slba=%d, nlb=%d", int, int , u16, void*, u64, u32);

TRACEPOINT(trace_nvme_req_done_error, "nvme%d qid=%d, cid=%d, status type=%#x, status code=%#x, bio=%#x", int, int, u16, u8, u8, bio*);
TRACEPOINT(trace_nvme_req_done_success, "nvme%d qid=%d, cid=%d, bio=%#x",int,int, u16, bio*);

TRACEPOINT(trace_nvme_admin_queue_wake, "nvme%d qid=%d",int,int);

TRACEPOINT(trace_nvme_admin_queue_submit, "nvme%d qid=%d, cid=%d",int, int, int);
TRACEPOINT(trace_nvme_admin_req_done_error, "nvme%d qid=%d, cid=%d, status type=%#x, status code=%#x", int, int, u16, u8, u8);
TRACEPOINT(trace_nvme_admin_req_done_success, "nvme%d qid=%d, cid=%d",int,int, u16);

TRACEPOINT(trace_advance_sq_tail_full, "nvme%d qid=%d, sq_tail=%d, sq_head=%d", int, int, int, int);
TRACEPOINT(trace_nvme_wait_for_entry, "nvme%d qid=%d, sq_tail=%d, sq_head=%d", int, int, int, int);

nvme_queue_pair::nvme_queue_pair(
    int did,
    u32 id,
    int qsize,
    pci::device &dev,
    nvme_sq_entry_t* sq_addr,
    u32* sq_doorbell,
    nvme_cq_entry_t* cq_addr,
    u32* cq_doorbell,
    std::map<u32, nvme_ns_t*>& ns)
          : _id(id)
          ,_driverid(did)
          ,_qsize(qsize)
          ,_dev(&dev)
          ,_sq_addr(sq_addr)
          ,_sq_head(0)
          ,_sq_tail(0)
          ,_sq_doorbell(sq_doorbell)
          ,_sq_full(false)
          ,_cq_addr(cq_addr)
          ,_cq_head(0)
          ,_cq_tail(0)
          ,_cq_doorbell(cq_doorbell)
          ,_cq_phase_tag(1)
          ,_ns(ns)

{
    auto prplists = (u64**) malloc(sizeof(u64*)*qsize);
    memset(prplists,0,sizeof(u64*)*qsize);
    _prplists_in_use.push_back(prplists);

    assert(!completion_queue_not_empty());
}

nvme_queue_pair::~nvme_queue_pair()
{
    memory::free_phys_contiguous_aligned(_sq_addr);
    memory::free_phys_contiguous_aligned(_cq_addr);
    for(auto vec: _prplists_in_use)
        memory::free_phys_contiguous_aligned(vec);
}

inline void nvme_queue_pair::advance_sq_tail()
{
    _sq_tail = (_sq_tail + 1) % _qsize;
    if(_sq_tail == _sq_head) {
        _sq_full = true; 
        trace_advance_sq_tail_full(_driverid,_id,_sq_tail,_sq_head);
    }
}

u16 nvme_queue_pair::submit_cmd(std::unique_ptr<nvme_sq_entry_t> cmd)
{   u16 ret;
    WITH_LOCK(_lock) 
    {
        ret = submit_cmd_without_lock(std::move(cmd));
    }
    return ret;
}

u16 nvme_queue_pair::submit_cmd_without_lock(std::unique_ptr<nvme_sq_entry_t> cmd)
{
    _sq_addr[_sq_tail] = *cmd;
    advance_sq_tail();
    mmio_setl(_sq_doorbell,_sq_tail);
    return _sq_tail;
}

void nvme_queue_pair::wait_for_completion_queue_entries()
{
    sched::thread::wait_until([this] {
        bool have_elements = this->completion_queue_not_empty();
        if (!have_elements) {
            this->enable_interrupts();
            //check if we got a new cqe between completion_queue_not_empty()
            //and enable_interrupts()
            have_elements = this->completion_queue_not_empty();
            if (have_elements) {
                this->disable_interrupts();
            }
        }

        trace_nvme_wait_for_completion_queue_entries(_driverid,_id,have_elements);
        return have_elements;
    });
}

int nvme_queue_pair::map_prps(u16 cid, void* data, u64 datasize, u64* prp1, u64* prp2)
{
    u64 addr = (u64) data;
    *prp1 = addr;
    *prp2 = 0;
    int numpages = 0;
    u64 offset = addr - ( (addr >> NVME_PAGESHIFT) << NVME_PAGESHIFT );
    if(offset) numpages = 1;

    numpages += ( datasize - offset + NVME_PAGESIZE - 1) >> NVME_PAGESHIFT;

    if (numpages == 2) {
        *prp2 = ((addr >> NVME_PAGESHIFT) +1 ) << NVME_PAGESHIFT;
    } else if (numpages > 2) {
        assert(numpages / 512 == 0);
        u64* prplist = (u64*) memory::alloc_phys_contiguous_aligned(numpages * 8, 4096);
        assert(prplist != nullptr);
        *prp2 = mmu::virt_to_phys(prplist);
        _prplists_in_use.at(cid / _qsize)[cid % _qsize] = prplist;
        
        addr = ((addr >> NVME_PAGESHIFT) +1 ) << NVME_PAGESHIFT;
        prplist[0] = addr;

        for (int i = 1; i < numpages - 1; i++) {
            addr += NVME_PAGESIZE;
            prplist[i] = addr;
        }
    }
    return 0;
}

std::unique_ptr<nvme_cq_entry_t> nvme_queue_pair::get_completion_queue_entry()
{
    if(!completion_queue_not_empty()) {
        return nullptr;
    }

    auto* tcqe = new nvme_cq_entry_t; 
    *tcqe = _cq_addr[_cq_head];
    std::unique_ptr<nvme_cq_entry_t> cqe(tcqe);
    assert(cqe->p == _cq_phase_tag);

    if(++_cq_head == _qsize) {
        _cq_head -= _qsize;
        _cq_phase_tag = !_cq_phase_tag;
    }
    return cqe; 
}


bool nvme_queue_pair::completion_queue_not_empty() const
{
    bool a = reinterpret_cast<volatile nvme_cq_entry_t*>(&_cq_addr[_cq_head])->p == _cq_phase_tag;
    trace_nvme_completion_queue_not_empty(_driverid,_id,a);
    return a;//_cq_addr[_cq_head].p == _cq_phase_tag;
}

void nvme_queue_pair::enable_interrupts()
{
    _dev->msix_unmask_entry(_id);
    trace_nvme_enable_interrupts(_driverid,_id);
}

void nvme_queue_pair::disable_interrupts()
{
    _dev->msix_mask_entry(_id);
    trace_nvme_disable_interrupts(_driverid,_id);
}

//only use with interrupts disabled
std::unique_ptr<nvme_cq_entry_t> nvme_queue_pair::check_for_completion(u16 cid)
{
    int msec = 1000;
    int timeout = 50;
    int i;

    std::unique_ptr<nvme_cq_entry_t> cqe;
    for(i = 0; i < timeout; i++) {
        if(completion_queue_not_empty()) {
            cqe = get_completion_queue_entry();
            assert(cqe->cid == cid);
            if(cqe->sct != 0 || cqe->sc != 0) {
                NVME_ERROR("polling nvme%d qid=%d, cid=%d, sct=%#x, sc=%#x\n", _driverid, _id, cid, cqe->sct, cqe->sc);
                _sq_head = cqe->sqhd; //update sq_head
                mmio_setl(_cq_doorbell, _cq_head);
                return cqe;
            }

            _sq_head = cqe->sqhd; //update sq_head
            mmio_setl(_cq_doorbell, _cq_head);
            return cqe;
        }
        usleep(msec);
    }
    NVME_ERROR("polling timeout nvme%d qid=%d cid=%d\n", _driverid, _id, cid);
    return cqe;
}

int nvme_io_queue_pair::make_request(bio* bio, u32 nsid=1)
{
    u64 slba = bio->bio_offset;
    u32 nlb = bio->bio_bcount; //do the blockshift in nvme_driver
    u16 cid;
    
    _lock.lock();
    cid = _sq_tail;
    if(_sq_full) {
        //Wait for free entries
        _waiter.reset(*sched::thread::current());
        trace_nvme_wait_for_entry(_driverid,_id,_sq_tail,_sq_head);
        sched::thread::wait_until([this] {return !(this->_sq_full);});
        _waiter.clear();
    }
    /* 
    We need to check if there is an outstanding command that uses 
    _sq_tail as command id.
    This happens if 
        1.The SQ is full. Then we just have to wait for an open slot (see above)
        2.the Controller already read a SQE but didnt post a CQE yet.
            This means we could post the command but need a different cid. To still
            use the cid as index to find the corresponding bios we use a matrix 
            adding columns if we need them
    */
    while(_pending_bios.at(cid / _qsize)[cid % _qsize]) {
        cid += _qsize;
        if(_pending_bios.size() <= (cid / _qsize)){
            auto bios_array = (struct bio**) malloc(sizeof(struct bio*) * _qsize);
            memset(bios_array,0,sizeof(struct bio*) * _qsize);
            _pending_bios.push_back(bios_array);
            auto prplists = (u64**) malloc(sizeof(u64*)* _qsize);
            memset(prplists,0,sizeof(u64*)* _qsize);
            _prplists_in_use.push_back(prplists);
        }
    }
    _pending_bios.at(cid / _qsize)[cid % _qsize] = bio;



    switch (bio->bio_cmd) {
    case BIO_READ:
        trace_nvme_read(_driverid, _id, cid, bio->bio_data, slba, nlb);
        submit_rw(cid,(void*)mmu::virt_to_phys(bio->bio_data),slba,nlb, nsid, NVME_CMD_READ);
        break;
    
    case BIO_WRITE:
        trace_nvme_write(_driverid, _id, cid, bio->bio_data, slba, nlb);
        submit_rw(cid,(void*)mmu::virt_to_phys(bio->bio_data),slba,nlb, nsid, NVME_CMD_WRITE);
        break;
    
    case BIO_FLUSH: {
        auto cmd = alloc_cmd(); 
        cmd->vs.common.opc = NVME_CMD_FLUSH;
        cmd->vs.common.nsid = nsid;
        cmd->vs.common.cid = cid;
        submit_cmd_without_lock(std::move(cmd));
        } break;
        
    default:
        NVME_ERROR("Operation not implemented\n");
        _lock.unlock();
        return ENOTBLK;
    }
    _lock.unlock();
    return 0;
}

void nvme_io_queue_pair::req_done()
{
    std::unique_ptr<nvme_cq_entry_t> cqe;
    u16 cid;
    while(true) 
    {
        wait_for_completion_queue_entries();
        trace_nvme_io_queue_wake(_driverid,_id);
        while((cqe = get_completion_queue_entry())) {
            cid = cqe->cid;
            if(cqe->sct != 0 || cqe->sc != 0) {
                trace_nvme_req_done_error(_driverid,_id, cid, cqe->sct, cqe->sc, _pending_bios.at(cid / _qsize)[cid % _qsize]);
                if(_pending_bios.at(cid / _qsize)[cid % _qsize])
                    biodone(_pending_bios.at(cid / _qsize)[cid % _qsize],false);
                NVME_ERROR("I/O queue: cid=%d, sct=%#x, sc=%#x, bio=%#x, slba=%llu, nlb=%llu\n",cqe->cid, cqe->sct, 
                    cqe->sc,_pending_bios.at(cid / _qsize)[cid % _qsize],
                    cqe->sc,_pending_bios.at(cid / _qsize)[cid % _qsize]->bio_offset,
                    cqe->sc,_pending_bios.at(cid / _qsize)[cid % _qsize]->bio_bcount);
            }else {
                trace_nvme_req_done_success(_driverid,_id, cid, _pending_bios.at(cid / _qsize)[cid % _qsize]);
                if(_pending_bios.at(cid / _qsize)[cid % _qsize])
                    biodone(_pending_bios.at(cid / _qsize)[cid % _qsize],true);
            }

            _pending_bios.at(cid / _qsize)[cid % _qsize] = nullptr;
            if(_prplists_in_use.at(cid / _qsize)[cid % _qsize]) {
                memory::free_phys_contiguous_aligned(_prplists_in_use.at(cid / _qsize)[cid % _qsize]);
                _prplists_in_use.at(cid / _qsize)[cid % _qsize] = nullptr;
            }
            _sq_head = cqe->sqhd; //update sq_head
        }
        mmio_setl(_cq_doorbell, _cq_head);
        if(_sq_full) { //wake up the requesting thread in case the submission queue was full before
            _sq_full = false;
            if(_waiter)
                _waiter.wake_from_kernel_or_with_irq_disabled();
        }
    }
}

int nvme_io_queue_pair::submit_rw(u16 cid, void* data, u64 slba, u32 nlb, u32 nsid, int opc)
{
    auto cmd = alloc_cmd();
    u64 prp1 = 0, prp2 = 0;
    u32 datasize = nlb << _ns[nsid]->blockshift;
    
    map_prps(cid, data, datasize, &prp1, &prp2);
    cmd->rw.common.cid = cid;
    cmd->rw.common.opc = opc;
    cmd->rw.common.nsid = nsid;
    cmd->rw.common.prp1 = prp1;
    cmd->rw.common.prp2 = prp2;
    cmd->rw.slba = slba;
    cmd->rw.nlb = nlb - 1;
        
    return submit_cmd_without_lock(std::move(cmd));
}

void nvme_admin_queue_pair::req_done()
{   
    std::unique_ptr<nvme_cq_entry_t> cqe;
    u16 cid;
    while(true) 
    {
        wait_for_completion_queue_entries();
        trace_nvme_admin_queue_wake(_driverid,_id);
        while((cqe = get_completion_queue_entry())) {
            cid = cqe->cid;
            if(cqe->sct != 0 || cqe->sc != 0) {
                trace_nvme_admin_req_done_error(_driverid,_id, cid, cqe->sct, cqe->sc);
                NVME_ERROR("Admin queue cid=%d, sct=%#x, sc=%#x\n",cid,cqe->sct,cqe->sc);
            }else {
                trace_nvme_admin_req_done_success(_driverid,_id, cid);
            }
            
            if(_prplists_in_use.at(cid / _qsize)[cid % _qsize]) {
                memory::free_phys_contiguous_aligned(_prplists_in_use.at(cid / _qsize)[cid % _qsize]);
                _prplists_in_use.at(cid / _qsize)[cid % _qsize] = nullptr;
            }
            _sq_head = cqe->sqhd; //update sq_head
            _req_res = std::move(cqe); //save the cqe so that the requesting thread can return it
        }
        mmio_setl(_cq_doorbell, _cq_head);
        
        /*Wake up the thread that requested the admin command*/
        new_cq = true;
        _req_waiter.wake_from_kernel_or_with_irq_disabled();
    }
}

std::unique_ptr<nvme_cq_entry_t> nvme_admin_queue_pair::submit_and_return_on_completion(std::unique_ptr<nvme_sq_entry_t> cmd, void* data, unsigned int datasize)
{
    _lock.lock();
    
    _req_waiter.reset(*sched::thread::current());
    
    //for now admin cid = sq_tail
    u16 cid = _sq_tail;
    cmd->rw.common.cid = cid;

    if(data != nullptr && datasize > 0) {
        map_prps(_sq_tail,data, datasize, &cmd->rw.common.prp1, &cmd->rw.common.prp2);
    }
    
    trace_nvme_admin_queue_submit(_driverid,_id,cid);
    submit_cmd_without_lock(std::move(cmd));
    
    sched::thread::wait_until([this] {return this->new_cq;});
    _req_waiter.clear();
    
    new_cq = false;
    if(_prplists_in_use.at(0)[cid]) {
        free(_prplists_in_use.at(0)[cid]);
    }
    
    _lock.unlock();
    return std::move(_req_res);
}

nvme_io_queue_pair::nvme_io_queue_pair(
        int did,
        int id,
        int qsize,
        pci::device& dev,

        nvme_sq_entry_t* sq_addr,
        u32* sq_doorbell,

        nvme_cq_entry_t* cq_addr,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
        ) : nvme_queue_pair(
            did,
            id,
            qsize,
            dev,

            sq_addr,
            sq_doorbell,

            cq_addr,
            cq_doorbell,
            ns
        ){
    auto bios_array = (bio**) malloc(sizeof(bio*) * qsize);
    memset(bios_array, 0, sizeof(bio*) * qsize);
    _pending_bios.push_back(bios_array);
}

nvme_io_queue_pair::~nvme_io_queue_pair()
{
    for(auto vec : _pending_bios)
        free(vec);
}

nvme_admin_queue_pair::nvme_admin_queue_pair(
        int did,
        int id,
        int qsize,
        pci::device& dev,

        nvme_sq_entry_t* sq_addr,
        u32* sq_doorbell,

        nvme_cq_entry_t* cq_addr,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
        ) : nvme_queue_pair(
            did,
            id,
            qsize,
            dev,

            sq_addr,
            sq_doorbell,

            cq_addr,
            cq_doorbell,
            ns
        ){};
