/*
 * Copyright (C) 2023 Jan Braunwarth
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef NVME_DRIVER_H
#define NVME_DRIVER_H

#include "drivers/nvme-structs.h"
#include "drivers/driver.hh"
#include "drivers/pci-device.hh"
#include <osv/mempool.hh>
#include <osv/interrupt.hh>
#include <osv/msi.hh>
#include <osv/aligned_new.hh>
#include "drivers/nvme-queue.hh"
#include <vector>
#include <memory>
#include <map>

#define NVME_QUEUE_PER_CPU_ENABLED 1

//Volatile Write Cache
#define NVME_VWC_ENABLED 1

#define NVME_ADMIN_QUEUE_SIZE 8

//Will be lower if the device doesnt support the specified queue size
#define NVME_IO_QUEUE_SIZE 64

namespace nvme {

enum NVME_IO_QUEUE_PRIORITY {
    NVME_IO_QUEUE_PRIORITY_URGENT = 0,
    NVME_IO_QUEUE_PRIORITY_HIGH = 1,
    NVME_IO_QUEUE_PRIORITY_MEDIUM = 2,
    NVME_IO_QUEUE_PRIORITY_LOW = 3,
};

class driver : public hw_driver {
public:
    explicit driver(pci::device& dev);
    virtual ~driver() {};

    virtual std::string get_name() const { return "nvme"; }

    virtual void dump_config();

    int make_request(struct bio* bio, u32 nsid = 1);
    static hw_driver* probe(hw_device* dev);

    std::map<u32, nvme_ns_t*> _ns_data;

private:
    int identify_controller();
    int identify_namespace(u32 ns);

    void create_admin_queue();
    void register_admin_interrupt();

    void create_io_queues();
    int create_io_queue(int qid, int qsize = NVME_IO_QUEUE_SIZE,
        sched::cpu* cpu = nullptr, int qprio = NVME_IO_QUEUE_PRIORITY_HIGH);
    bool register_io_interrupt(unsigned int iv, unsigned int qid,
        sched::cpu* cpu = nullptr);

    void init_controller_config();

    void read_capabilities();
    int reset_controller();
    int enable_controller();
    int wait_for_controller_ready_change(int ready);

    int set_number_of_queues(u16 num, u16* ret);
    int set_interrupt_coalescing(u8 threshold, u8 time);

    bool parse_pci_config();
    void enable_msix();

    void enable_write_cache();

    bool msix_register(unsigned iv,
        // high priority ISR
        std::function<void ()> isr,
        // bottom half
        sched::thread *t,
        // set affinity of the vector to the cpu running t
        bool assign_affinity = false);

    //Maintains the nvme instance number for multiple adapters
    static int _instance;
    int _id;

    //Disk index number
    static int _disk_idx;

    std::vector<std::unique_ptr<msix_vector>> _msix_vectors;

    std::unique_ptr<admin_queue_pair, aligned_new_deleter<admin_queue_pair>> _admin_queue;

    std::vector<std::unique_ptr<io_queue_pair, aligned_new_deleter<io_queue_pair>>> _io_queues;
    u32 _doorbell_stride;
    unsigned int _ready_timeout;

    std::unique_ptr<nvme_identify_ctlr_t> _identify_controller;
    nvme_controller_reg_t* _control_reg = nullptr;

    pci::device& _dev;
    interrupt_manager _msi;

    pci::bar *_bar0 = nullptr;
};

}
#endif
