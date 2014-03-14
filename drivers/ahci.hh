/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef AHCI_DRIVER_H
#define AHCI_DRIVER_H

#include "driver.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>
#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/bio.h>
#include <osv/types.h>

namespace ahci {

// ATA Command
enum ata_cmd {
    ATA_CMD_READ_DMA = 0xC8,
    ATA_CMD_READ_DMA_EXT = 0x25,
    ATA_CMD_WRITE_DMA = 0xCA,
    ATA_CMD_WRITE_DMA_EXT = 0x35,
    ATA_CMD_FLUSH_CACHE_EXT = 0xEA,
    ATA_CMD_IDENTIFY_DEVICE = 0xEC,
    ATA_CMD_IDENTIFY_PACKET_DEVICE = 0xA1,
};

// Generic Host Control
enum hba_reg {
    HOST_CAP        = 0x00,
    HOST_GHC        = 0x04,
    HOST_IS         = 0x08,
    HOST_PI         = 0x0C,
    HOST_VS         = 0x10,
    HOST_CCC_CTL    = 0x14,
    HOST_CCC_PORTS  = 0x18,
    HOST_EM_LOC     = 0x1C,
    HOST_EM_CTL     = 0x20,
};

// HOST_GHC bits
enum hba_reg_ghc_bits {
    HOST_GHC_HR     = 1U << 0,
    HOST_GHC_IE     = 1U << 1,
    HOST_GHC_MSI    = 1U << 2,
    HOST_GHC_AE     = 1U << 31,
};

// Port Registers
enum port_reg {
    PORT_CLB        = 0x00,
    PORT_CLBU       = 0x04,
    PORT_FB         = 0x08,
    PORT_FBU        = 0x0C,
    PORT_IS         = 0x10,
    PORT_IE         = 0x14,
    PORT_CMD        = 0x18,
    PORT_TFD        = 0x20,
    PORT_SIG        = 0x24,
    PORT_SSTS       = 0x28,
    PORT_SCTL       = 0x2C,
    PORT_SERR       = 0x30,
    PORT_SACT       = 0x34,
    PORT_CI         = 0x38,
    PORT_SNTF       = 0x3C,
    PORT_VS         = 0x70,
};

// PORT_CMD bits
enum port_reg_cmd_bits {
    PORT_CMD_ST     = 1U << 0,
    PORT_CMD_SUD    = 1U << 1,
    PORT_CMD_POD    = 1U << 2,
    PORT_CMD_CLO    = 1U << 3,
    PORT_CMD_FRE    = 1U << 4,
    PORT_CMD_FR     = 1U << 14,
    PORT_CMD_CR     = 1U << 15,
    PORT_CMD_ATAPI  = 1U << 24,
};

// PORT_TFD bits
enum port_reg_tfd_bits {
    PORT_TFD_ERR    = 1U << 0,
    PORT_TFD_DRQ    = 1U << 3,
    PORT_TFD_BSY    = 1U << 7,
};

// PORT_IE bits
enum port_reg_ie_bits {
    PORT_IE_DHRE    = 1U << 0,
    PORT_IE_SDBE    = 1U << 3,
};

// PORT_IS bits
enum port_reg_is_bits {
    PORT_IS_DHRS    = 1U << 0,
    PORT_IS_SDBS    = 1U << 3,
};

// PORT_SSTS bits
enum port_reg_ssts_bits {
    PORT_SSTS_DET       = 0x0F,
    PORT_SSTS_LINKUP    = 0x03,
    PORT_SSTS_RETRY     = 0x0F,
};

// CIFS: Command FIS
struct cfis {
    // DWORD 0
    u8 fis_type;
    u8 flags;
    u8 command;
    u8 feature_low;

    // DWORD 1
    u8 lba_low;
    u8 lba_mid;
    u8 lba_high;
    u8 device;

    // DWORD 2
    u8 lba_ext_low;
    u8 lba_ext_mid;
    u8 lba_ext_high;
    u8 feature_high;

    // DWORD 3
    u8 sector_count;
    u8 sector_count_ext;
    u8 icc;
    u8 control;

    // DWORD 3
    u8 reserved[4];

    // PAD
    u8 pad[64 - 20];
} __attribute__((packed));

// PRDT: Physical Region Descriptor Table
struct prdt {
    u32 base;
    u32 baseu;
    u32 reserved;
    u32 flags; // 4MB per entry at most
} __attribute__((packed));

// Command Table Structure
struct cmd_table {
    struct cfis fis;
    u8 atapi_cmd[0x10];
    u8 reserved[0x30];
    struct prdt prdt[1]; // TODO: support more descriptors
} __attribute__((packed));

// Command List Structure
struct cmd_list {
    u32 flags;
    u32 bytes;
    u32 base;
    u32 baseu;
    u32 reserved[4];
} __attribute__((packed));

// Received FIS Structure
struct recv_fis {
    u8 dsfis[0x1c];     // DSFIS: DMA Setup FIS
    u8 reserved1[0x04];
    u8 psfis[0x14];     // PSFIS: PIO Setup FIS
    u8 reserved2[0x0c];
    u8 rfis[0x14];      // RFIS: D2H Register FIS
    u8 reserved3[0x04];
    u8 sdbfis[0x08];    // SDBFIS: Set Device Bits FIS
    u8 ufis[0x40];      // UFIS: Unknow FIS
    u8 reserved4[0x60];
} __attribute__((packed));

enum {
    AHCI_VENDOR_ID_VBOX = 0x8086,
    AHCI_DEVICE_ID_VBOX = 0x2829,
    AHCI_VENDOR_ID_VMW = 0x15AD,
    AHCI_DEVICE_ID_VMW = 0x07E0,
    AHCI_VENDOR_ID_QEMU = 0x8086,
    AHCI_DEVICE_ID_QEMU = 0x2922,
};

class port;

class hba : public hw_driver {
public:
    hba(pci::device& pci_dev);
    ~hba();

    static struct hba_priv *get_priv(struct bio *bio) {
        return reinterpret_cast<struct hba_priv*>(bio->bio_dev->private_data);
    }
    bool poll_mode() { return _poll_mode; }
    pci::device& pci_device() { return _pci_dev; }
    static hw_driver* probe(hw_device* hw_dev);
    const std::string get_name() { return _driver_name; }
    bool parse_pci_config();
    void dump_config();
    bool ack_irq();
    void enable_irq();
    void reset();
    void setup();
    void scan();
    void add_port(u32 pnr, port * port);

    // Access ahci config space
    u8 hba_readb(u32 offset) { return _bar6->readb(offset);};
    u16 hba_readw(u32 offset) { return _bar6->readw(offset);};
    u32 hba_readl(u32 offset) { return _bar6->readl(offset);};
    void hba_writeb(u32 offset, u8 val) { _bar6->writeb(offset, val);};
    void hba_writew(u32 offset, u16 val) { _bar6->writew(offset, val);};
    void hba_writel(u32 offset, u32 val) { _bar6->writel(offset, val);};

private:
    std::map<int, port *> _all_ports;
    std::string _driver_name;
    gsi_level_interrupt _gsi;
    bool _poll_mode = false;
    pci::device& _pci_dev;
    interrupt_manager _msi;
    static int _disk_idx;
    pci::bar *_bar6;
};

class port {
public:
    port(u32 pnr, hba *hba);
    ~port();

    void reset();
    void setup();
    int send_cmd(u8 slot, int iswrite, void *buffer, u32 bsize);
    void wait_cmd_poll(u8 slot);
    void wait_cmd_irq(u8 slot);
    void disk_identify();
    void disk_flush(struct bio *bio);
    void disk_rw(struct bio *bio, bool iswrite);
    int make_request(struct bio *bio);
    void enable_irq();
    void wait_device_ready();
    void wait_ci_ready(u8 slot);
    void wakeup() { _waiter.wake(); }
    void inc_cmd_done_nr() { _cmd_done_nr++; }
    u64 get_cmd_done_nr() { return _cmd_done_nr.load(std::memory_order_relaxed); }
    bool linkup() { return _linkup; }

    u32 port2hba(u32 port_reg)
    {
        u32 reg = 0x100;
        reg += _pnr * 0x80;
        reg += port_reg;
        return reg;
    }

    u32 port_readl(u32 reg)
    {
        u32 hba_reg = port2hba(reg);
        return _hba->hba_readl(hba_reg);
    }

    void port_writel(u32 reg, u32 val)
    {
        u32 hba_reg = port2hba(reg);
        _hba->hba_writel(hba_reg, val);
    }

    size_t get_devsize() { return _devsize; }

    u8 recv_fis_error()
    {
        u8 error  = _recv_fis->rfis[3];
        return error;
    }

private:
    std::atomic<uint64_t> _cmd_done_nr{0};
    sched::thread_handle _waiter;
    u64 _cmd_send_nr = 0;
    bool _linkup = false;
    u8 _queue_depth;
    size_t _devsize;
    mutex _lock;
    u32 _pnr;
    hba *_hba;

    struct cmd_list *_cmd_list;
    struct cmd_table *_cmd_table;
    struct recv_fis *_recv_fis;
    mmu::phys _cmd_list_pa;
    mmu::phys _cmd_table_pa;
    mmu::phys _recv_fis_pa;

};

}
#endif
