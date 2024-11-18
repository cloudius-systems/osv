/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/cdefs.h>

#include <vector>
#include <memory>

#include <osv/contiguous_alloc.hh>
#include <osv/bio.h>
#include <osv/trace.hh>
#include <osv/mempool.hh>
#include <osv/align.hh>

#include "nvme-queue.hh"

TRACEPOINT(trace_nvme_cq_wait, "nvme%d qid=%d, cq_head=%d", int, int, int);
TRACEPOINT(trace_nvme_cq_woken, "nvme%d qid=%d, have_elements=%d", int, int, bool);
TRACEPOINT(trace_nvme_cq_not_empty, "nvme%d qid=%d, not_empty=%d", int, int, bool);
TRACEPOINT(trace_nvme_cq_head_advance, "nvme%d qid=%d cq_head=%d", int, int, int);
TRACEPOINT(trace_nvme_cq_new_entry, "nvme%d qid=%d sqhd=%d", int, int, int);

TRACEPOINT(trace_nvme_enable_interrupts, "nvme%d qid=%d", int, int);
TRACEPOINT(trace_nvme_disable_interrupts, "nvme%d qid=%d", int, int);

TRACEPOINT(trace_nvme_req_done_error, "nvme%d qid=%d, cid=%d, status type=%#x, status code=%#x, bio=%p", int, int, u16, u8, u8, bio*);
TRACEPOINT(trace_nvme_req_done_success, "nvme%d qid=%d, cid=%d, bio=%p", int, int, u16, bio*);

TRACEPOINT(trace_nvme_admin_cmd_submit, "nvme%d qid=%d, cid=%d, opc=%d", int, int, int, u8);
TRACEPOINT(trace_nvme_read_write_cmd_submit, "nvme%d qid=%d cid=%d, bio=%p, slba=%d, nlb=%d, write=%d", int, int, u16, void*, u64, u32, bool);

TRACEPOINT(trace_nvme_sq_tail_advance, "nvme%d qid=%d, sq_tail=%d, sq_head=%d, depth=%d, full=%d", int, int, int, int, int, bool);
TRACEPOINT(trace_nvme_sq_full_wait, "nvme%d qid=%d, sq_tail=%d, sq_head=%d", int, int, int, int);
TRACEPOINT(trace_nvme_sq_full_wake, "nvme%d qid=%d, sq_tail=%d, sq_head=%d", int, int, int, int);

TRACEPOINT(trace_nvme_cid_conflict, "nvme%d qid=%d, cid=%d", int, int, int);

TRACEPOINT(trace_nvme_prp_alloc, "nvme%d qid=%d, prp=%p", int, int, void*);
TRACEPOINT(trace_nvme_prp_free, "nvme%d qid=%d, prp=%p", int, int, void*);

using namespace memory;

namespace nvme {

queue_pair::queue_pair(
    int did,
    u32 id,
    int qsize,
    pci::device &dev,
    u32* sq_doorbell,
    u32* cq_doorbell,
    std::map<u32, nvme_ns_t*>& ns)
      : _id(id)
      ,_driver_id(did)
      ,_qsize(qsize)
      ,_dev(&dev)
      ,_sq(sq_doorbell)
      ,_sq_full(false)
      ,_cq(cq_doorbell)
      ,_cq_phase_tag(1)
      ,_ns(ns)
{
    size_t sq_buf_size = qsize * sizeof(nvme_sq_entry_t);
    _sq._addr = (nvme_sq_entry_t*) alloc_phys_contiguous_aligned(sq_buf_size, mmu::page_size);
    assert(_sq._addr);
    memset(_sq._addr, 0, sq_buf_size);

    size_t cq_buf_size = qsize * sizeof(nvme_cq_entry_t);
    _cq._addr = (nvme_cq_entry_t*) alloc_phys_contiguous_aligned(cq_buf_size, mmu::page_size);
    assert(_cq._addr);
    memset(_cq._addr, 0, cq_buf_size);

    assert(!completion_queue_not_empty());
}

queue_pair::~queue_pair()
{
    u64* free_prp;
    while (_free_prp_lists.pop(free_prp))
       free_page((void*)free_prp);

    free_phys_contiguous_aligned(_sq._addr);
    free_phys_contiguous_aligned(_cq._addr);
}

inline void queue_pair::advance_sq_tail()
{
    _sq._tail = (_sq._tail + 1) % _qsize;
    if (((_sq._tail + 1) % _qsize) == _sq._head) {
        _sq_full = true;
    }
    trace_nvme_sq_tail_advance(_driver_id, _id, _sq._tail, _sq._head,
        (_sq._tail >= _sq._head) ? _sq._tail - _sq._head : _sq._tail + (_qsize - _sq._head),
         _sq_full);
}

u16 queue_pair::submit_cmd(nvme_sq_entry_t* cmd)
{
    _sq._addr[_sq._tail] = *cmd;
    advance_sq_tail();
    mmio_setl(_sq._doorbell, _sq._tail);
    return _sq._tail;
}

void queue_pair::wait_for_completion_queue_entries()
{
    trace_nvme_cq_wait(_driver_id, _id, _cq._head);
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

        trace_nvme_cq_woken(_driver_id, _id, have_elements);
        return have_elements;
    });
}

void queue_pair::map_prps(nvme_sq_entry_t* cmd, struct bio* bio, u64 datasize)
{
    void* data = (void*)mmu::virt_to_phys(bio->bio_data);
    bio->bio_private = nullptr;

    // Depending on the datasize, we map PRPs (Physical Region Page) as follows:
    // 0. We always set the prp1 field to the beginning of the data
    // 1. If data falls within single 4K page then we simply set prp2 to 0
    // 2. If data falls within 2 pages then set prp2 to the second 4K-aligned part of data
    // 3. Otherwise, allocate a physically contigous array long enough to hold addresses
    //    of remaining 4K pages of data
    u64 addr = (u64) data;
    cmd->rw.common.prp1 = addr;
    cmd->rw.common.prp2 = 0;

    // Calculate number of 4K pages and therefore number of entries in the PRP
    // list. The 1st entry rw.common.prp1 can be misaligned but every
    // other one needs to be 4K-aligned
    u64 first_page_start = align_down(addr, NVME_PAGESIZE);
    u64 last_page_end = align_up(addr + datasize, NVME_PAGESIZE);
    int num_of_pages = (last_page_end - first_page_start) / NVME_PAGESIZE;

    if (num_of_pages == 2) {
        cmd->rw.common.prp2 = first_page_start + NVME_PAGESIZE; //2nd page start
    } else if (num_of_pages > 2) {
        // Allocate PRP list as the request is larger than 8K
        // For now we can only accomodate datasize <= 2MB so single page
        // should be exactly enough to map up to 512 pages of the request data
        assert(num_of_pages / 512 == 0);
        u64* prp_list = nullptr;
        _free_prp_lists.pop(prp_list);
        if (!prp_list) { // No free pre-allocated ones, so allocate new one
            prp_list = (u64*) alloc_page();
            trace_nvme_prp_alloc(_driver_id, _id, prp_list);
        }

        assert(prp_list != nullptr);
        cmd->rw.common.prp2 = mmu::virt_to_phys(prp_list);

        // Save PRP list in bio so it can be de-allocated later
        bio->bio_private = prp_list;

        // Fill in the PRP list with address of subsequent 4K pages
        addr = first_page_start + NVME_PAGESIZE; //2nd page start
        prp_list[0] = addr;

        for (int i = 1; i < num_of_pages - 1; i++) {
            addr += NVME_PAGESIZE;
            prp_list[i] = addr;
        }
    }
}

nvme_cq_entry_t* queue_pair::get_completion_queue_entry()
{
    if (!completion_queue_not_empty()) {
        return nullptr;
    }

    auto* cqe = &_cq._addr[_cq._head];
    assert(cqe->p == _cq_phase_tag);

    trace_nvme_cq_new_entry(_driver_id, _id, cqe->sqhd);
    return cqe;
}

inline void queue_pair::advance_cq_head()
{
    trace_nvme_cq_head_advance(_driver_id, _id, _cq._head);
    if (++_cq._head == _qsize) {
        _cq._head = 0;
        _cq_phase_tag = _cq_phase_tag ? 0 : 1;
    }
}

bool queue_pair::completion_queue_not_empty() const
{
    bool a = reinterpret_cast<volatile nvme_cq_entry_t*>(&_cq._addr[_cq._head])->p == _cq_phase_tag;
    trace_nvme_cq_not_empty(_driver_id, _id, a);
    return a;
}

void queue_pair::enable_interrupts()
{
    _dev->msix_unmask_entry(_id);
    trace_nvme_enable_interrupts(_driver_id, _id);
}

void queue_pair::disable_interrupts()
{
    _dev->msix_mask_entry(_id);
    trace_nvme_disable_interrupts(_driver_id, _id);
}

io_queue_pair::io_queue_pair(
    int driver_id,
    int id,
    int qsize,
    pci::device& dev,
    u32* sq_doorbell,
    u32* cq_doorbell,
    std::map<u32, nvme_ns_t*>& ns
    ) : queue_pair(
        driver_id,
        id,
        qsize,
        dev,
        sq_doorbell,
        cq_doorbell,
        ns
    )
{
    init_pending_bios(0);
}

io_queue_pair::~io_queue_pair()
{
    for (auto bios : _pending_bios) {
        if (bios) {
            free(bios);
        }
    }
}

void io_queue_pair::init_pending_bios(u32 level)
{
    _pending_bios[level] = (std::atomic<struct bio*> *) malloc(sizeof(std::atomic<struct bio*>) * _qsize);
    for (u32 idx = 0; idx < _qsize; idx++) {
        _pending_bios[level][idx] = {};
    }
}

int io_queue_pair::make_request(struct bio* bio, u32 nsid = 1)
{
    u64 slba = bio->bio_offset;
    u32 nlb = bio->bio_bcount; //do the blockshift in nvme_driver

    SCOPE_LOCK(_lock);
    if (_sq_full) {
        //Wait for free entries
        _sq_full_waiter.reset(*sched::thread::current());
        trace_nvme_sq_full_wait(_driver_id, _id, _sq._tail, _sq._head);
        sched::thread::wait_until([this] { return !(this->_sq_full); });
        _sq_full_waiter.clear();
    }
    assert((((_sq._tail + 1) % _qsize) != _sq._head));
    //
    // We need to check if there is an outstanding command that uses
    // _sq._tail as command id.
    // This happens if:
    // 1. The SQ is full. Then we just have to wait for an open slot (see above)
    // 2. The Controller already read a SQE but didnt post a CQE yet.
    //    This means we could post the command but need a different cid. To still
    //    use the cid as index to find the corresponding bios we use a matrix
    //    adding columns if we need them
    u16 cid = _sq._tail;
    while (_pending_bios[cid_to_row(cid)][cid_to_col(cid)].load()) {
        trace_nvme_cid_conflict(_driver_id, _id, cid);
        cid += _qsize;
        auto level = cid_to_row(cid);
        assert(level < max_pending_levels);
        // Allocate next row of _pending_bios if needed
        if (!_pending_bios[cid_to_row(cid)]) {
            init_pending_bios(level);
        }
    }
    //Save bio
    _pending_bios[cid_to_row(cid)][cid_to_col(cid)] = bio;

    switch (bio->bio_cmd) {
    case BIO_READ:
        trace_nvme_read_write_cmd_submit(_driver_id, _id, cid, bio, slba, nlb, false);
        submit_read_write_cmd(cid, nsid, NVME_CMD_READ, slba, nlb, bio);
        break;

    case BIO_WRITE:
        trace_nvme_read_write_cmd_submit(_driver_id, _id, cid, bio, slba, nlb, true);
        submit_read_write_cmd(cid, nsid, NVME_CMD_WRITE, slba, nlb, bio);
        break;

    case BIO_FLUSH:
        submit_flush_cmd(cid, nsid);
        break;

    default:
        NVME_ERROR("Operation not implemented\n");
        return ENOTBLK;
    }
    return 0;
}

void io_queue_pair::req_done()
{
    nvme_cq_entry_t* cqep = nullptr;
    while (true)
    {
        wait_for_completion_queue_entries();
        while ((cqep = get_completion_queue_entry())) {
            // Read full CQ entry onto stack so we can advance CQ head ASAP
            // and release the CQ slot
            nvme_cq_entry_t cqe = *cqep;
            advance_cq_head();
            mmio_setl(_cq._doorbell, _cq._head);
            //
            // Wake up the requesting thread in case the submission queue was full before
            auto old_sq_head = _sq._head.exchange(cqe.sqhd); //update sq_head
            if (old_sq_head != cqe.sqhd && _sq_full) {
                _sq_full = false;
                if (_sq_full_waiter) {
                     trace_nvme_sq_full_wake(_driver_id, _id, _sq._tail, _sq._head);
                    _sq_full_waiter.wake_from_kernel_or_with_irq_disabled();
                }
            }
            //
            // Read cid and release it
            u16 cid = cqe.cid;
            auto pending_bio = _pending_bios[cid_to_row(cid)][cid_to_col(cid)].exchange(nullptr);
            assert(pending_bio);
            //
            // Save for future re-use or free PRP list saved under bio_private if any
            if (pending_bio->bio_private) {
                if (!_free_prp_lists.push((u64*)pending_bio->bio_private)) {
                   free_page(pending_bio->bio_private); //_free_prp_lists is full so free the page
                   trace_nvme_prp_free(_driver_id, _id, pending_bio->bio_private);
                }
            }
            // Call biodone
            if (cqe.sct != 0 || cqe.sc != 0) {
                trace_nvme_req_done_error(_driver_id, _id, cid, cqe.sct, cqe.sc, pending_bio);
                biodone(pending_bio, false);
                NVME_ERROR("I/O queue: cid=%d, sct=%#x, sc=%#x, bio=%#x, slba=%llu, nlb=%llu\n",
                    cqe.cid, cqe.sct, cqe.sc, pending_bio,
                    pending_bio ? pending_bio->bio_offset : 0,
                    pending_bio ? pending_bio->bio_bcount : 0);
            } else {
                trace_nvme_req_done_success(_driver_id, _id, cid, pending_bio);
                biodone(pending_bio, true);
            }
        }
    }
}

u16 io_queue_pair::submit_read_write_cmd(u16 cid, u32 nsid, int opc, u64 slba, u32 nlb, struct bio* bio)
{
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.rw.common.cid = cid;
    cmd.rw.common.opc = opc;
    cmd.rw.common.nsid = nsid;
    cmd.rw.slba = slba;
    cmd.rw.nlb = nlb - 1;

    u32 datasize = nlb << _ns[nsid]->blockshift;
    map_prps(&cmd, bio, datasize);

    return submit_cmd(&cmd);
}

u16 io_queue_pair::submit_flush_cmd(u16 cid, u32 nsid)
{
    nvme_sq_entry_t cmd;
    memset(&cmd, 0, sizeof(cmd));

    cmd.vs.common.opc = NVME_CMD_FLUSH;
    cmd.vs.common.nsid = nsid;
    cmd.vs.common.cid = cid;

    return submit_cmd(&cmd);
}

admin_queue_pair::admin_queue_pair(
    int driver_id,
    int id,
    int qsize,
    pci::device& dev,
    u32* sq_doorbell,
    u32* cq_doorbell,
    std::map<u32, nvme_ns_t*>& ns
    ) : queue_pair(
        driver_id,
        id,
        qsize,
        dev,
        sq_doorbell,
        cq_doorbell,
        ns
) {}

void admin_queue_pair::req_done()
{
    nvme_cq_entry_t* cqe = nullptr;
    while (true)
    {
        wait_for_completion_queue_entries();
        while ((cqe = get_completion_queue_entry())) {
            u16 cid = cqe->cid;
            if (cqe->sct != 0 || cqe->sc != 0) {
                trace_nvme_req_done_error(_driver_id, _id, cid, cqe->sct, cqe->sc, nullptr);
                NVME_ERROR("Admin queue cid=%d, sct=%#x, sc=%#x\n",cid,cqe->sct,cqe->sc);
            } else {
                trace_nvme_req_done_success(_driver_id, _id, cid, nullptr);
            }

            _sq._head = cqe->sqhd; //Update sq_head
            _req_res = *cqe; //Save the cqe so that the requesting thread can return it

            advance_cq_head();
        }
        mmio_setl(_cq._doorbell, _cq._head);

        //Wake up the thread that requested the admin command
        new_cq = true;
        _req_waiter.wake_from_kernel_or_with_irq_disabled();
    }
}

nvme_cq_entry_t
admin_queue_pair::submit_and_return_on_completion(nvme_sq_entry_t* cmd, void* data, unsigned int datasize)
{
    SCOPE_LOCK(_lock);

    _req_waiter.reset(*sched::thread::current());

    //for now admin cid = sq_tail
    u16 cid = _sq._tail;
    cmd->rw.common.cid = cid;

    if (data != nullptr && datasize > 0) {
        cmd->rw.common.prp1 = (u64)data;
        cmd->rw.common.prp2 = 0;
    }

    trace_nvme_admin_cmd_submit(_driver_id, _id, cid, cmd->set_features.common.opc);
    submit_cmd(cmd);

    sched::thread::wait_until([this] { return this->new_cq; });
    _req_waiter.clear();

    new_cq = false;

    return _req_res;
}
}
