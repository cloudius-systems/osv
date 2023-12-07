#ifndef NVME_DRIVER_H
#define NVME_DRIVER_H

#include "drivers/nvme-structs.h"
#include "drivers/driver.hh"
#include "drivers/pci-device.hh"
#include <osv/mempool.hh>
#include <osv/interrupt.hh>
#include <osv/msi.hh>
#include "drivers/nvme-queue.hh"
#include <vector>
#include <memory>
#include <map>

#define nvme_tag "nvme"
#define nvme_d(...)    tprintf_d(nvme_tag, __VA_ARGS__)
#define nvme_i(...)    tprintf_i(nvme_tag, __VA_ARGS__)
#define nvme_w(...)    tprintf_w(nvme_tag, __VA_ARGS__)
#define nvme_e(...)    tprintf_e(nvme_tag, __VA_ARGS__)

#define NVME_ERROR(...) nvme_e(__VA_ARGS__)

#define NVME_PAGESIZE 4096
#define NVME_PAGESHIFT 12

/*bdev block cache will not be used if enabled*/
#define NVME_DIRECT_RW_ENABLED 0

#define NVME_QUEUE_PER_CPU_ENABLED 0

//Volatile Write Cache
#define NVME_VWC_ENABLED 1

//checks for all active namespaces instead of just ns 1
#define NVME_CHECK_FOR_ADDITIONAL_NAMESPACES 1

#define NVME_ADMIN_QUEUE_SIZE 8

/*Will be lower if the device doesnt support the
specified queue size */ 
#define NVME_IO_QUEUE_SIZE 256

class nvme_io_queue_pair;
class nvme_admin_queue_pair;

class nvme : public hw_driver {
public:
    explicit nvme(pci::device& dev);
    virtual ~nvme() {};

    virtual std::string get_name() const { return "nvme"; }

    virtual void dump_config();

    int make_request(struct bio* bio, u32 nsid=1);
    static hw_driver* probe(hw_device* dev);

    int set_feature();
    int get_feature();

    int set_number_of_queues(u16 num, u16* ret);
    int set_interrupt_coalescing(u8 threshold, u8 time);

    int get_interrupt_coalescing();

    int create_io_queue(int qsize=NVME_IO_QUEUE_SIZE, int qprio=2);
    
    bool register_interrupt(unsigned int iv,unsigned int qid,bool pin_t=false, sched::cpu* cpu = NULL);

    int shutdown();

    std::map<u32, nvme_ns_t*> _ns_data;
    
private:
    int identify_controller();
    int identify_namespace(u32 ns);
    int identify_active_namespaces(u32 start);

    void create_admin_queue();
    void register_admin_interrupts();

    void init_controller_config();
    void create_io_queues_foreach_cpu();

    void enable_controller();
    void disable_controller();
    int wait_for_controller_ready_change(int ready);

    void parse_pci_config();

    nvme_controller_reg_t* _control_reg;
    
    //maintains the nvme instance number for multiple adapters
    static int _instance;
    int _id;

    std::vector<std::unique_ptr<msix_vector>> _msix_vectors;
    bool msix_register(unsigned iv,
    // high priority ISR
    std::function<void ()> isr,
    // bottom half
    sched::thread *t,
    // set affinity of the vector to the cpu running t
    bool assign_affinity=false);

    std::unique_ptr<nvme_admin_queue_pair> _admin_queue;

    std::vector<std::unique_ptr<nvme_io_queue_pair>> _io_queues;
    u32 _doorbellstride;

    std::unique_ptr<nvme_identify_ctlr_t> _identify_controller;

    pci::device& _dev;
    interrupt_manager _msi;

    pci::bar *_bar0 = nullptr;
};
#endif
