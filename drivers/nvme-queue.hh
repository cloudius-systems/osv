#ifndef NVME_QUEUE_H
#define NVME_QUEUE_H

#include "drivers/nvme.hh"

class nvme_queue_pair;

class nvme_queue_pair 
{
public:
    nvme_queue_pair(
        int did,
        u32 id,
        int qsize,
        pci::device& dev,

        nvme_sq_entry_t* sq_addr,
        u32* sq_doorbell,

        nvme_cq_entry_t* cq_addr,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
        );

    ~nvme_queue_pair();
    
    u16 submit_cmd(std::unique_ptr<nvme_sq_entry_t> cmd);
    
    virtual void req_done() {};
    void wait_for_completion_queue_entries();
    bool completion_queue_not_empty() const;

    void enable_interrupts();
    void disable_interrupts();

    u32 _id;
protected:
    int _driverid;

    u32 _qsize;
    pci::device* _dev;

    nvme_sq_entry_t* _sq_addr;
    u32 _sq_head;
    u32 _sq_tail;
    volatile u32* _sq_doorbell;
    bool _sq_full;

    nvme_cq_entry_t* _cq_addr;
    u32 _cq_head;
    u32 _cq_tail;
    volatile u32* _cq_doorbell;
    int _cq_phase_tag;

    std::map<u32, nvme_ns_t*> _ns;

    std::vector<u64**> _prplists_in_use;
    
    mutex _lock;
    sched::thread_handle _waiter;

    void advance_sq_tail();
    int map_prps(u16 cid, void* data, u64 datasize, u64* prp1, u64* prp2);

    u16 submit_cmd_without_lock(std::unique_ptr<nvme_sq_entry_t> cmd);

    u16 submit_cmd_batch_without_lock(std::vector<std::unique_ptr<nvme_sq_entry_t>> cmds);

    std::unique_ptr<nvme_cq_entry_t> get_completion_queue_entry();

    std::unique_ptr<nvme_cq_entry_t> check_for_completion(u16 cid);
};

class nvme_io_queue_pair : public nvme_queue_pair {
public:
    nvme_io_queue_pair(
        int did,
        int id,
        int qsize,
        pci::device& dev,

        nvme_sq_entry_t* sq_addr,
        u32* sq_doorbell,

        nvme_cq_entry_t* cq_addr,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
    );
    ~nvme_io_queue_pair();

    int self_test();
    int make_request(struct bio* bio, u32 nsid);
    void req_done();

    int submit_io_batch(std::vector<bio*> bios, u32 nsid=1);
private:
    std::vector<struct bio**> _pending_bios;
    int submit_rw(u16 cid, void* data, u64 slba, u32 nlb, u32 nsid, int opc);
    int submit_flush();
};

class nvme_admin_queue_pair : public nvme_queue_pair {
public:
    nvme_admin_queue_pair(
        int did,
        int id,
        int qsize,
        pci::device& dev,

        nvme_sq_entry_t* sq_addr,
        u32* sq_doorbell,

        nvme_cq_entry_t* cq_addr,
        u32* cq_doorbell,
        std::map<u32, nvme_ns_t*>& ns
    );

    std::unique_ptr<nvme_cq_entry_t> _req_res;
    volatile bool new_cq;
    void req_done();
    std::unique_ptr<nvme_cq_entry_t> submit_and_return_on_completion(std::unique_ptr<nvme_sq_entry_t> cmd, void* data=nullptr, unsigned int datasize=0);
private:
    sched::thread_handle _req_waiter;
};

#endif