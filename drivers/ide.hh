/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef IDE_H
#define IDE_H
#include "driver.hh"
#include "drivers/pci.hh"
#include "drivers/driver.hh"
#include "drivers/pci-function.hh"
#include "drivers/pci-device.hh"
#include <osv/bio.h>
#include <osv/interrupt.hh>

namespace ide {

class ide_drive : public hw_driver {
public:
    explicit ide_drive(pci::device& dev);
    virtual ~ide_drive();

    virtual const std::string get_name(void) { return _driver_name; }

    virtual void dump_config(void);

    int make_request(struct bio*);

    int64_t size();

    void set_readonly() {_ro = true;}
    bool is_readonly() {return _ro;}

    static hw_driver* probe(hw_device* dev);
private:
    static constexpr int SECTOR_SIZE = 512;

    enum ide_register_base {
        PORT0 = 0x1f0,
        PORT1 = 0x3f6,
        PORT2 = 0x170,
        PORT3 = 0x376
    };

    enum ide_register {
        REG_DATA = 0x00,
        REG_ERROR = 0x01,
        REG_FEATURES = 0x01,
        REG_SECCOUNT0 = 0x02,
        REG_LBA0 = 0x03,
        REG_LBA1 = 0x04,
        REG_LBA2 = 0x05,
        REG_HDDEVSEL = 0x06,
        REG_COMMAND = 0x07,
        REG_STATUS = 0x07,
        REG_SECCOUNT1 = 0x08,
        REG_LBA3 = 0x09,
        REG_LBA4 = 0x0a,
        REG_LBA5 = 0x0b,
        REG_CONTROL = 0x0c,
        REG_ALTSTATUS = 0x0c,
        REG_DEFVADDRESS = 0x0d
    };

    enum ide_command {
        CMD_READ_PIO = 0x20,
        CMD_READ_PIO_EXT = 0x24,
        CMD_WRITE_PIO = 0x30,
        CMD_WRITE_PIO_EXT = 0x34,
        CMD_FLUSH = 0xe7,
        CMD_FLUSH_EXT = 0xea,
        CMD_IDENTIFY = 0xec
    };

    enum ide_status {
        STATUS_BSY = 0x80,
        STATUS_DRDY = 0x40,
        STATUS_DRQ = 0x08,
        STATUS_DF = 0x20,
        STATUS_ERR = 0x01
    };

    enum ide_control {
        CONTROL_SRST = 0x04,
        CONTROL_NIEN = 0x02
    };

    enum ide_identify {
        IDENTIFY_LBA_EXT = 100
    };

    static int poll(u8 mask = (STATUS_BSY|STATUS_DRDY), u8 match = STATUS_DRDY);
    static void start(uint8_t cmd, u32 sector, unsigned dev, void *data, u8 cnt, bool head);
    static void reg_writeb(int base, int reg, u8 val);
    static void reg_writesl(int base, int reg, void *buf, int cnt);
    static u8 reg_readb(int base, int reg);
    static void reg_readsl(int base, int reg, void *buf, int cnt);

    pci::device& _dev;
    std::string _driver_name;

    //maintains the ide instance number for multiple drives
    static int _instance;
    int _id;
    bool _ro;
    // This mutex protects parallel make_request invocations
    mutex _lock;
};

}
#endif

