/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/ahci.hh"
#include <string.h>
#include <osv/debug.h>
#include <osv/mmu.hh>
#include <osv/device.h>
#include <osv/bio.h>
#include <osv/types.h>

namespace ahci {

int hba::_disk_idx = 0;

struct hba_priv {
    devop_strategy_t strategy;
    class hba *hba;
    class port *port;
};

static void hba_strategy(struct bio *bio)
{
    auto prv = hba::get_priv(bio);
    prv->port->make_request(bio);
}

static int hba_read(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_read(dev, uio, ioflags);
}

static int hba_write(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_write(dev, uio, ioflags);
}

static struct devops hba_devops {
    no_open,
    no_close,
    hba_read,
    hba_write,
    no_ioctl,
    no_devctl,
    multiplex_strategy,
};

struct driver hba_driver = {
    "ahci_hba",
    &hba_devops,
    sizeof(struct hba_priv),
};

port::port(u32 pnr, hba *hba)
    : _pnr(pnr),
      _hba(hba)
{
    reset();
    setup();
    if (!linkup()) {
        return;
    }
    disk_identify();
    enable_irq();
}

port::~port()
{
    memory::free_phys_contiguous_aligned(_cmd_list);
    memory::free_phys_contiguous_aligned(_cmd_table);
    memory::free_phys_contiguous_aligned(_recv_fis);
}

void port::reset()
{
    // Disable FIS and Command
    for (;;) {
        auto cmd = port_readl(PORT_CMD);
        if (!(cmd & (PORT_CMD_FRE | PORT_CMD_FR | PORT_CMD_ST | PORT_CMD_CR)))
            break;
        cmd &= ~(PORT_CMD_FRE | PORT_CMD_ST);
        port_writel(PORT_CMD, cmd);
    }

    // Disable all IRQs.
    port_writel(PORT_IE, 0);

    // Clear all IRQs
    auto is = port_readl(PORT_IS);
    port_writel(PORT_IS, is);
}

void port::setup()
{
    // Setup Command List and Received FIS Structure
    // sz = 32 * 32 = 1024
    auto sz = sizeof(*_cmd_list) * 32;
    _cmd_list = reinterpret_cast<struct cmd_list *>(
                memory::alloc_phys_contiguous_aligned(sz, sz));
    _cmd_list_pa = mmu::virt_to_phys(_cmd_list);
    memset(_cmd_list, 0, sz);

    // sz = 256 * 32 = 8192, cmd_table need to be aligned to 128 bytes
    sz = sizeof(*_cmd_table) * 32;
    _cmd_table = reinterpret_cast<struct cmd_table *>(
                 memory::alloc_phys_contiguous_aligned(sz, 128));
    _cmd_table_pa = mmu::virt_to_phys(_cmd_table);
    memset(_cmd_table, 0, sz);

    // size = 256 * 1 = 256
    sz = sizeof(*_recv_fis) * 1;
    _recv_fis = reinterpret_cast<struct recv_fis *>(
                memory::alloc_phys_contiguous_aligned(sz, sz));
    _recv_fis_pa = mmu::virt_to_phys(_recv_fis);
    memset(_recv_fis, 0, sz);

    port_writel(PORT_CLB, _cmd_list_pa & 0xFFFFFFFF);
    port_writel(PORT_CLBU, _cmd_list_pa >> 32);

    port_writel(PORT_FB, _recv_fis_pa & 0xFFFFFFFF);
    port_writel(PORT_FBU, _recv_fis_pa >> 32);

    // Enable FIS
    auto cmd = port_readl(PORT_CMD);
    cmd |= PORT_CMD_FRE;
    port_writel(PORT_CMD, cmd);

    // Power and Spin up port
    cmd |= PORT_CMD_SUD | PORT_CMD_POD;
    port_writel(PORT_CMD, cmd);

    int count = 0;
    for (;;) {
        auto stat = port_readl(PORT_SSTS);
        if ((stat & PORT_SSTS_DET) == PORT_SSTS_LINKUP) {
            // AHCI Port Link up
            _linkup = true;
            break;
        }
        if (count++ > PORT_SSTS_RETRY) {
            return;
        }
    }

    // Clear Error Status
    auto err = port_readl(PORT_SERR);
    port_writel(PORT_SERR, err);

    // Wait for Device Becoming Ready
    wait_device_ready();

    // Start Device
    cmd |= PORT_CMD_ST;
    port_writel(PORT_CMD, cmd);

    // Register the per-port irq thread
    std::string name("ahci-port");
    name += std::to_string(_pnr);
    _irq_thread = new sched::thread([this] { this->req_done(); },
            sched::thread::attr().name(name));
    _irq_thread->start();
}

void port::enable_irq()
{
    // Enable Device to Host Register FIS interrupt
    auto val = port_readl(PORT_IE);
    val |= PORT_IE_DHRE;
    port_writel(PORT_IE, val);
}

void port::wait_device_ready()
{
    // Wait for Device Becoming Ready
    for (;;) {
        auto tfd = port_readl(PORT_TFD);
        if (!(tfd & (PORT_TFD_BSY | PORT_TFD_DRQ)))
            break;
    }
}

void port::wait_ci_ready(u8 slot)
{
    // Wait for Command Issue Becoming Ready
    for (;;) {
        auto ci = port_readl(PORT_CI);
        if (!(ci & (1U << slot)))
            break;
    }
}

int port::send_cmd(u8 slot, int iswrite, void *buffer, u32 bsize)
{
    u32 flags;

    // Setup Command List
    flags = ((buffer ? (1U << 16) : 0) | // One PRD Entry
             (5U << 0)) |                //FIS Length 5 DWORDS, 20 Bytes
             (iswrite ? (1U << 6) : 0);
    mmu::phys base = mmu::virt_to_phys(&_cmd_table[slot]);
    // cmd_table needs to be aligned on 128-bytes, indicated by bit 6-0 being zero.
    assert((base & 0x7F) == 0);
    _cmd_list[slot].flags = flags;
    _cmd_list[slot].bytes = 0;
    _cmd_list[slot].base = base & 0xFFFFFFFF;
    _cmd_list[slot].baseu = base >> 32;

    if (buffer) {
        // Setup Command Table
        mmu::phys buf = mmu::virt_to_phys(buffer);
        // Data Base Address must be 2-bytes aligned, indicated by bit 0 being zero.
        assert((buf & 0x01) == 0);
        _cmd_table[slot].prdt[0].base = buf & 0xFFFFFFFF;
        _cmd_table[slot].prdt[0].baseu = buf >> 32;
        // Data Byte Count. Bit ‘0’ of this field must always be ‘1’ to indicate an even byte count.
        assert(((bsize - 1) & 0x01) == 1);
        _cmd_table[slot].prdt[0].flags = bsize - 1;
    }

    // Use _cmd_lock to close the following race:
    // Cmd send             Cmd completion
    // 1) set _cmd_active
    //                      2) read PORT_CI for cmd completion
    //                         done_mask would think this cmd was completed
    // 3) set PORT_CI
    WITH_LOCK(_cmd_lock) {
        _cmd_active |= 1U << slot;
        port_writel(PORT_CI, 1U << slot);
    }

    return 0;
}

void port::wait_cmd_poll(u8 slot)
{
    auto host_is = _hba->hba_readl(HOST_IS);
    for (;;) {
        auto is = port_readl(PORT_IS);
        if (is) {
            port_writel(PORT_IS, is);

            wait_device_ready();
            wait_ci_ready(slot);

           if (is & 0x02) {
               auto error  = _recv_fis->psfis[3];
               assert(!error);
               break;
           }

           if (is & 0x01) {
               auto error  = recv_fis_error();
               assert(!error);
               break;
            }
        }
    }
    _hba->hba_writel(HOST_IS, host_is);

    _cmd_active &= ~(1U << slot);
}

u32 port::done_mask()
{
    if (_cmd_active == 0x0)
        return 0x0;

    if (!used_slot())
        return 0x0;

    auto ci = port_readl(PORT_CI);

    return _cmd_active & (~ci);
}

void port::req_done()
{
    while (1) {
        u32 mask;

        WITH_LOCK(_cmd_lock) {
            sched::thread::wait_until(_cmd_lock, [&] { mask = this->done_mask(); return mask != 0x0; });
        }

        while (mask) {
            u8 slot = ffs(mask) - 1;
            assert(slot >= 0 && slot < 32);

            struct bio *bio = _bios[slot].load(std::memory_order_relaxed);
            _bios[slot] = nullptr;
            assert(bio != nullptr);

            biodone(bio, true);

            mask &= ~(1U << slot);

            // Mark the slot available
            _cmd_active &= ~(1U << slot);

            // Increase slot free number
            _slot_free++;

            // Wakeup the thread waiting for a free slot
            _cmd_send_waiter.wake();
        }
    }
}

int port::make_request(struct bio* bio)
{
    WITH_LOCK(_lock) {
        if (!bio)
            return EIO;

        switch (bio->bio_cmd) {
        case BIO_READ:
            disk_rw(bio, false);
            break;
        case BIO_WRITE:
            disk_rw(bio, true);
            break;
        case BIO_FLUSH:
            disk_flush(bio);
            break;
        default:
            return ENOTBLK;
        }
        return 0;
    }
}

void port::poll_mode_done(struct bio *bio, u8 slot)
{
    if (_hba->poll_mode()) {
        wait_cmd_poll(slot);
        biodone(bio, true);
    }
}

void port::disk_rw(struct bio *bio, bool iswrite)
{
    auto len = bio->bio_bcount;
    auto buf = bio->bio_data;
    u64 lba = bio->bio_offset / 512;
    u32 nr_sec = len / 512;
    u8 command, slot;

    slot = get_slot_wait();

    struct cmd_table &cmd = _cmd_table[slot];

    memset(&cmd.fis, 0, sizeof(cmd.fis));
    command = (iswrite ? ATA_CMD_WRITE_DMA_EXT : ATA_CMD_READ_DMA_EXT);
    cmd.fis.fis_type = 0x27;
    cmd.fis.flags = 1 << 7;
    cmd.fis.feature_low = 1;
    cmd.fis.command = command;
    cmd.fis.sector_count = nr_sec & 0xFF;
    cmd.fis.sector_count_ext = (nr_sec >> 8) & 0xFF;
    cmd.fis.lba_low = lba & 0xFF;
    cmd.fis.lba_mid = (lba >> 8) & 0xFF;
    cmd.fis.lba_high = (lba >> 16) & 0xFF;
    cmd.fis.lba_ext_low = (lba >> 24) & 0xFF;
    cmd.fis.lba_ext_mid = (lba >> 32) & 0xFF;
    cmd.fis.lba_ext_high = (lba >> 40) & 0xFF;
    cmd.fis.device = (1 << 6) | (1 << 4); // must have bit 6 set

    _bios[slot] = bio;
    send_cmd(slot, iswrite, buf, len);

    poll_mode_done(bio, slot);
}

void port::disk_flush(struct bio *bio)
{
    auto slot = get_slot_wait();

    struct cmd_table &cmd = _cmd_table[slot];

    memset(&cmd.fis, 0, sizeof(cmd.fis));
    cmd.fis.fis_type = 0x27;
    cmd.fis.flags = 1 << 7;
    cmd.fis.command = ATA_CMD_FLUSH_CACHE_EXT;

    _bios[slot] = bio;
    send_cmd(slot, 0, nullptr, 0);

    poll_mode_done(bio, slot);
}

void port::disk_identify()
{
    u8 slot = 0;
    struct cmd_table &cmd = _cmd_table[slot];
    auto buffer = new u16[256];
    u64 sectors;

    memset(&cmd.fis, 0, sizeof(cmd.fis));
    cmd.fis.fis_type = 0x27;
    cmd.fis.flags = 1 << 7;
    cmd.fis.command = ATA_CMD_IDENTIFY_DEVICE;

    send_cmd(slot, 0, buffer, 512);
    wait_cmd_poll(slot);

    // Word 75 queue depth
    _queue_depth = buffer[75] & 0x1F;

    // Word 83 LBA48 support
    if (buffer[83] & (1 << 10))  {
        // Word 100 to 103
        sectors = *(u64*)&buffer[100];
    } else {
        // Word 60 and 61
        sectors = *(u32*)&buffer[60];
    }

    _devsize = sectors * 512;

    delete [] buffer;
}

bool port::used_slot()
{
    return _slot_free.load(std::memory_order_relaxed) != _slot_nr;
}

bool port::avail_slot()
{
    return _slot_free.load(std::memory_order_relaxed) >= 1;
}

bool port::get_slot(u8 &slot)
{
    if (!avail_slot()) {
        return false;
    }

    _slot_free--;

    slot = ffs(~_cmd_active) - 1;

    return true;
}

u8 port::get_slot_wait()
{
    u8 slot;
    while (!get_slot(slot)) {
        _cmd_send_waiter.reset(*sched::thread::current());
        sched::thread::wait_until([this] {return this->avail_slot();});
        _cmd_send_waiter.clear();
    }
    return slot;
}

void hba::enable_irq()
{
    if (poll_mode()) {
        return;
    }

    // Enable Interrupts
    auto ghc = hba_readl(HOST_GHC);
    ghc |= HOST_GHC_IE;
    hba_writel(HOST_GHC, ghc);

    if (_pci_dev.is_msix() || _pci_dev.is_msi() ) {
        _msi.easy_register({ { 0, [=] { ack_irq(); }, nullptr} });
    } else {
        _gsi.set_ack_and_handler(_pci_dev.get_interrupt_line(), [=] { return ack_irq(); }, [] {});
    }
}

hba::hba(pci::device& pci_dev)
    : hw_driver()
    , _pci_dev(pci_dev)
    , _msi(&pci_dev)
{

    _driver_name = "ahci";
    parse_pci_config();

    reset();
    setup();
    enable_irq();
    scan();
}

hba::~hba()
{
    for (auto it : this->_all_ports) {
        auto port = it.second;
        delete port;
    }
}

void hba::reset()
{
    auto val = hba_readl(HOST_GHC);

    // Disable Interrupt
    val &= HOST_GHC_IE;
    hba_writel(HOST_GHC, val);

    // Set AE and HR to 1 to reset the HBA
    val |= HOST_GHC_AE;
    hba_writel(HOST_GHC, val);

    val |= HOST_GHC_HR;
    hba_writel(HOST_GHC, val);

    // Wait reset of HBA to finish
    for (;;) {
        val = hba_readl(HOST_GHC);
        if ((val & HOST_GHC_HR) == 0x00)
            break;
    }
}

void hba::setup()
{
    // Enable HBA
    auto val = hba_readl(HOST_GHC);
    val |= HOST_GHC_AE;
    hba_writel(HOST_GHC, val);

    // Clear Pending Interrupts
    val = hba_readl(HOST_IS);
    hba_writel(HOST_IS, val);
}

void hba::scan()
{
    auto caps = hba_readl(HOST_CAP);
    auto ports = hba_readl(HOST_PI);
    auto max = caps & 0x1f;
    for (u32 pnr = 0; pnr <= max; pnr++) {
        if (!(ports & (1U << pnr)))
            continue;

        auto p = new port(pnr, this);
        if (!p->linkup()) {
            delete p;
            continue;
        }

        std::string dev_name("vblk");
        dev_name += std::to_string(_disk_idx++);
        auto dev = device_create(&hba_driver, dev_name.c_str(), D_BLK);
        auto prv = static_cast<struct hba_priv*>(dev->private_data);
        prv->hba = this;
        prv->port = p;
        prv->strategy = hba_strategy;
        dev->size = p->get_devsize();
        dev->max_io_size = 4 * 1024 * 1024; // one PRDT entry contains 4MB at most

        add_port(pnr, p);
        read_partition_table(dev);

        debug("AHCI: Add sata device port %d as %s, devsize=%lld\n", pnr, dev_name.c_str(), dev->size);
    }
}

void hba::add_port(u32 pnr, port *port)
{
    _all_ports.insert(std::make_pair(pnr, port));
}

void hba::dump_config()
{
    u8 B, D, F;

    _pci_dev.get_bdf(B, D, F);

    _pci_dev.dump_config();
}

bool hba::parse_pci_config()
{
    if (!_pci_dev.parse_pci_config()) {
        return false;
    }

    // Test whether bar6 is present
    _bar6 = _pci_dev.get_bar(6);
    if (_bar6 == nullptr) {
        return false;
    }
    _bar6->map();

    return true;
}

bool hba::ack_irq()
{
    bool handled = false;

    if (poll_mode())
        return handled;

    auto host_is = hba_readl(HOST_IS);
    if (!host_is)
        return handled;

    for (auto it : this->_all_ports) {
        auto port = it.second;

        // Clear PORT_IS
        auto is = port->port_readl(PORT_IS);
        port->port_writel(PORT_IS, is);

        u8 error = port->recv_fis_error();
        assert (error == 0);
        if ((is & 1)) {
            port->wakeup();
            handled = true;
        }
    }

    // Clear HOST_IS
    hba_writel(HOST_IS, host_is);

    return handled;
}

hw_driver* hba::probe(hw_device* hw_dev)
{
    if (auto pci_dev = dynamic_cast<pci::device*>(hw_dev)) {
        auto id = pci_dev->get_id();
        if (id == hw_device_id(AHCI_VENDOR_ID_VBOX, AHCI_DEVICE_ID_VBOX) ||
            id == hw_device_id(AHCI_VENDOR_ID_VMW, AHCI_DEVICE_ID_VMW) ||
            id == hw_device_id(AHCI_VENDOR_ID_QEMU, AHCI_DEVICE_ID_QEMU)) {
            return new hba(*pci_dev);
        }
    }
    return nullptr;
}

}
