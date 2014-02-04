/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <sys/cdefs.h>

#include "drivers/ide.hh"
#include "drivers/pci-device.hh"
#include <osv/interrupt.hh>

#include <osv/mempool.hh>
#include <osv/mmu.hh>

#include <string>
#include <string.h>
#include <map>
#include <errno.h>
#include <osv/debug.h>

#include <osv/sched.hh>
#include "osv/trace.hh"

#include <osv/device.h>
#include <osv/bio.h>

namespace ide {

int ide_drive::_instance = 0;
struct ide_priv {
    ide_drive* drv;
};

static void
ide_strategy(struct bio *bio)
{
    struct ide_priv *prv = reinterpret_cast<struct ide_priv*>(bio->bio_dev->private_data);

    bio->bio_offset += bio->bio_dev->offset;
    prv->drv->make_request(bio);
}

static int
ide_read(struct device *dev, struct uio *uio, int ioflags)
{
    return bdev_read(dev, uio, ioflags);
}

static int
ide_write(struct device *dev, struct uio *uio, int ioflags)
{
    auto* prv = reinterpret_cast<struct ide_priv*>(dev->private_data);

    if (prv->drv->is_readonly()) return EROFS;

    return bdev_write(dev, uio, ioflags);
}

static struct devops ide_devops {
    no_open,
    no_close,
    ide_read,
    ide_write,
    no_ioctl,
    no_devctl,
    ide_strategy,
};

struct driver ide_driver = {
    "ide",
    &ide_devops,
    sizeof(struct ide_priv),
};

ide_drive::ide_drive(pci::device& pci_dev)
    : _dev(_dev), _ro(false)
{
    _driver_name = "ide";
    _id = _instance++;
    /* Currently this driver is hard coded for primary master drive */
    assert(_instance == 1);

    struct ide_priv* prv;
    struct device *dev;
    std::string dev_name("vblk0");

    dev = device_create(&ide_driver, dev_name.c_str(), D_BLK);
    prv = reinterpret_cast<struct ide_priv*>(dev->private_data);
    prv->drv = this;
    dev->size = prv->drv->size();
    read_partition_table(dev);

    debugf("ide: Add ide device instances %d as %s, devsize=%lld\n", _id, dev_name.c_str(), dev->size);
}

ide_drive::~ide_drive()
{
    //TODO: In theory maintain the list of free instances and gc it
    // including the thread objects and their stack
}

int64_t ide_drive::size()
{
    unsigned short buf[256];
    int64_t *sectors;

    poll();
    reg_writeb(PORT0, REG_COMMAND, CMD_IDENTIFY);
    poll();
    reg_readsl(PORT0, REG_DATA, buf, sizeof(buf)/4);
    sectors = reinterpret_cast<int64_t *>(&buf[IDENTIFY_LBA_EXT]);
    return *sectors * SECTOR_SIZE;
}

int ide_drive::make_request(struct bio* bio)
{
    // The lock is here for parallel requests protection
    WITH_LOCK(_lock) {

        if (!bio) return EIO;

        switch (bio->bio_cmd) {
        case BIO_READ:
        case BIO_FLUSH:
            break;
        case BIO_WRITE:
            if (is_readonly()) {
                biodone(bio, false);
                return EROFS;
            }
            break;
        default:
            return ENOTBLK;
        }

        size_t len = 0;
        auto offset = reinterpret_cast<size_t>(bio->bio_data) & 0x1ff;
        auto *base = bio->bio_data;
        auto sector = bio->bio_offset / SECTOR_SIZE;
        auto count = bio->bio_bcount / SECTOR_SIZE;
        assert(offset == 0); // only sector aligned ptr is supported 
        assert(bio->bio_bcount <= 131072); // less than 128k

        while (len != bio->bio_bcount) {
            auto size = std::min(bio->bio_bcount - len, static_cast<size_t>(SECTOR_SIZE));
            assert (size == SECTOR_SIZE); // only sector size io is supported
            if (offset + size > SECTOR_SIZE)
                size = SECTOR_SIZE - offset;
            len += size;

            start(bio->bio_cmd, sector, 0, base, count, base == bio->bio_data);

            base += size;
            sector++;
            offset = 0;
        }
        reg_readb(PORT0, REG_STATUS);
        reg_readb(PORT0, REG_ALTSTATUS);

        biodone(bio, true);
        return 0;
    }
}

hw_driver* ide_drive::probe(hw_device* dev)
{
    /* Only support primary:master drive */
    if (_instance > 0)
        return nullptr;
    if (auto pci_dev = dynamic_cast<pci::device*>(dev)) {
        if (pci_dev->get_base_class_code() == pci::function::PCI_CLASS_STORAGE ||
            pci_dev->get_sub_class_code() == pci::function::PCI_SUB_CLASS_STORAGE_IDE) {

            /* Reset controller */
            reg_writeb(PORT0, REG_CONTROL, CONTROL_SRST);
            sched::thread::sleep(std::chrono::milliseconds(2));
            reg_writeb(PORT0, REG_CONTROL, 0);
            sched::thread::sleep(std::chrono::nanoseconds(400));

            /* Select primary master */
            reg_writeb(PORT0, REG_HDDEVSEL, 0xe0 | (0<<4));

            /* Disable intterupt for primary master/slave */
            reg_writeb(PORT0, REG_CONTROL, CONTROL_NIEN);
            reg_writeb(PORT1, REG_CONTROL, CONTROL_NIEN);

            /* Send CMD_IDENTIFY */
            reg_writeb(PORT0, REG_COMMAND, CMD_IDENTIFY);
            if (reg_readb(PORT0, REG_STATUS) == 0) /* No drive */
                return nullptr;
            return new ide_drive(*pci_dev);
        }
    }
    return nullptr;
}

// Wait for IDE disk to become ready.
int
ide_drive::poll(u8 mask, u8 match)
{
    int status;

    while (((status = reg_readb(PORT0, REG_STATUS)) & mask) != match)
        sched::thread::sleep(std::chrono::nanoseconds(400));
    reg_readb(PORT0, REG_ALTSTATUS);
    return status;
}

// Request one sector read/write.
void
ide_drive::start(uint8_t cmd, u32 sector, unsigned dev, void *data, u8 cnt, bool head)
{
    if (head) {
        poll();
        reg_writeb(PORT0, REG_SECCOUNT0, cnt);
        reg_writeb(PORT0, REG_LBA0, sector & 0xff);
        reg_writeb(PORT0, REG_LBA1, (sector >> 8) & 0xff);
        reg_writeb(PORT0, REG_LBA2, (sector >> 16) & 0xff);
        /* Select primary master */
        reg_writeb(PORT0, REG_HDDEVSEL,
            0xe0 | (0<<4) | ((sector >> 24) & 0x0f));
    }
    switch (cmd) {
    case BIO_READ:
        if (head)
            reg_writeb(PORT0, REG_COMMAND, CMD_READ_PIO);
        poll();
        reg_readsl(PORT0, REG_DATA, data, SECTOR_SIZE/4);
        break;
    case BIO_WRITE:
        if (head)
            reg_writeb(PORT0, REG_COMMAND, CMD_WRITE_PIO);
        reg_writesl(PORT0, REG_DATA, data, SECTOR_SIZE/4);
        poll();
        break;
    case BIO_FLUSH:
        if (head) {
            reg_writeb(PORT0, REG_COMMAND, CMD_FLUSH);
            poll();
        }
        break;
    default:
        abort("Invalid cmd");
    }
}

void ide_drive::dump_config()
{
}

inline void ide_drive::reg_writeb(int base, int reg, u8 val)
{
    processor::outb(val, base + reg);
}

inline void ide_drive::reg_writesl(int base, int reg, void *buf, int cnt)
{
    processor::outsl(buf, cnt, base + reg);
}

inline u8 ide_drive::reg_readb(int base, int reg)
{
    return processor::inb(base + reg);
}

inline void ide_drive::reg_readsl(int base, int reg, void *buf, int cnt)
{
    processor::insl(buf, cnt, base + reg);
}
}
