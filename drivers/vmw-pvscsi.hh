/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VMW_PVSCSI_DRIVER_H
#define VMW_PVSCSI_DRIVER_H

#include "drivers/virtio.hh"
#include "drivers/pci-device.hh"
#include "drivers/scsi-common.hh"
#include <osv/bio.h>
#include <osv/types.h>

namespace vmw {

// PVSCSI command flags
enum class pvscsi_cmd_flag: u32 {
    dir_tohost          = 1 << 3,
    dir_todevice        = 1 << 4
};

// PVSCSI reg offset
enum class pvscsi_reg_off: u32 {
    command             = 0x0,
    command_data        = 0x4,
    command_status      = 0x8,
    intr_status         = 0x100C,
    intr_mask           = 0x2010,
    kick_non_rw_io      = 0x3014,
    kick_rw_io          = 0x4018
};

// PVSCSI commands
enum class pvscsi_cmd: u32 {
    first               = 0,
    adapter_reset       = 1,
    issue_scsi          = 2,
    setup_rings         = 3,
    reset_bus           = 4,
    reset_device        = 5,
    abort_cmd           = 6,
    config              = 7,
    setup_msg_ring      = 8,
    device_unplug       = 9,
    last                = 10
};

enum {
    PVSCSI_SIMPLE_QUEUE_TAG     = 0x20,
    PVSCSI_INTR_CMPL_MASK       = 0x03,
};

enum {
    PVSCSI_VENDOR_ID_VMW         = 0x15AD,
    PVSCSI_DEVICE_ID_VMW         = 0x07C0,
};

struct pvscsi_cmd_desc_setup_rings {
    u32 req_ring_pages;
    u32 cmp_ring_pages;
    u64 ring_state_ppn;
    // 32 is the max number of page supportted.
    u64 req_ring_ppn[32];
    u64 cmp_ring_ppn[32];
} __attribute__((packed));

struct pvscsi_ring_state {
    u32 req_prod_idx;
    u32 req_cons_idx;
    u32 req_nr;

    u32 cmp_prod_idx;
    u32 cmp_cons_idx;
    u32 cmp_nr;

    u8  reserved[104];

    u32 msg_prod_idx;
    u32 msg_cons_idx;
    u32 msg_nr;
} __attribute__((packed));

struct pvscsi_ring_req_desc {
    u64 context;
    u64 data_addr;
    u64 data_len;
    u64 sense_addr;
    u32 sense_len;
    u32 flags;
    u8 cdb[16];
    u8 cdb_len;
    u8 lun[8];
    u8 tag;
    u8 bus;
    u8 target;
    u8 vcpu_hint;
    u8 reserved[59];
} __attribute__((packed));

struct pvscsi_ring_cmp_desc {
    u64 context;
    u64 data_len;
    u32 sense_len;
    u16 host_status;
    u16 scsi_status;
    u32 reserved[2];
} __attribute__((packed));

struct pvscsi_ring_msg_desc {
    u32 type;
    u32 args[31];
} __attribute__((packed));

class scsi_pvscsi_req: public scsi_common_req {
public:
    scsi_pvscsi_req(struct bio *bio, u16 target, u16 lun, u8 cmd)
        : scsi_common_req(bio, target, lun, cmd) { };
};

class pvscsi : public hw_driver, public scsi_common {
public:
    pvscsi(pci::device& dev);
    ~pvscsi();

    virtual const std::string get_name() { return _driver_name; }
    static struct pvscsi_priv *get_priv(struct bio *bio) {
        return reinterpret_cast<struct pvscsi_priv*>(bio->bio_dev->private_data);
    }
    virtual int make_request(struct bio*) override;
    virtual void add_lun(u16 target_id, u16 lun_id) override;
    virtual int exec_cmd(struct bio *bio) override;
    virtual scsi_pvscsi_req *alloc_scsi_req(struct bio *bio, u16 target, u16 lun, u8 cmd) override
    {
        return new scsi_pvscsi_req(bio, target, lun, cmd);
    }

    void req_done();
    bool ack_irq();
    void dump_config();
    bool parse_pci_config();

    static hw_driver* probe(hw_device* dev);
    void setup();

    // Access config space
    u8 readb(pvscsi_reg_off offset) { return _bar->readb(static_cast<u32>(offset));};
    u16 readw(pvscsi_reg_off offset) { return _bar->readw(static_cast<u32>(offset));};
    u32 readl(pvscsi_reg_off offset) { return _bar->readl(static_cast<u32>(offset));};
    void writeb(pvscsi_reg_off offset, u8 val) { _bar->writeb(static_cast<u32>(offset), val);};
    void writew(pvscsi_reg_off offset, u16 val) { _bar->writew(static_cast<u32>(offset), val);};
    void writel(pvscsi_reg_off offset, u32 val) { _bar->writel(static_cast<u32>(offset), val);};

    void write_cmd_desc(pvscsi_cmd cmd, void *desc, size_t len);
    void add_desc_wait(struct bio *bio);
    bool add_desc(struct bio *bio);
    void kick_desc(u8 cdb);
    bool avail_desc();
    u32 mask(u32 n) { return (1U << n) - 1; }

private:
    std::string _driver_name;
    pci::bar *_bar;
    pci::device& _pci_dev;
    interrupt_manager _msi;

    // Maintains the vmw-pvscsi instance number for multiple drives
    static int _instance;
    int _id;

    // Disk index number
    static int _disk_idx;

    // This mutex protects parallel make_request invocations
    mutex _lock;

    pvscsi_ring_state *_ring_state;
    pvscsi_ring_req_desc *_ring_req;
    pvscsi_ring_cmp_desc *_ring_cmp;
    mmu::phys _ring_state_pa;
    mmu::phys _ring_req_pa;
    mmu::phys _ring_cmp_pa;
    sched::thread_handle _waiter;
    std::atomic<u32> _req_free;
    u32 _req_depth;
};

}
#endif
