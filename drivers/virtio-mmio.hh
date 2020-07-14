/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_MMIO_DEVICE_HH
#define VIRTIO_MMIO_DEVICE_HH

#include <osv/types.h>
#include <osv/mmio.hh>
#include "virtio-device.hh"
#include "virtio-vring.hh"

using namespace hw;

/* Magic value ("virt" string) - Read Only */
#define VIRTIO_MMIO_MAGIC_VALUE		0x000

/* Virtio device version - Read Only */
#define VIRTIO_MMIO_VERSION		0x004

/* Virtio device ID - Read Only */
#define VIRTIO_MMIO_DEVICE_ID		0x008

/* Virtio vendor ID - Read Only */
#define VIRTIO_MMIO_VENDOR_ID		0x00c

/* Bitmask of the features supported by the device (host)
 * (32 bits per set) - Read Only */
#define VIRTIO_MMIO_DEVICE_FEATURES	0x010

/* Device (host) features set selector - Write Only */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL	0x014

/* Bitmask of features activated by the driver (guest)
 * (32 bits per set) - Write Only */
#define VIRTIO_MMIO_DRIVER_FEATURES	0x020

/* Activated features set selector - Write Only */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL	0x024

/* Queue selector - Write Only */
#define VIRTIO_MMIO_QUEUE_SEL		0x030

/* Maximum size of the currently selected queue - Read Only */
#define VIRTIO_MMIO_QUEUE_NUM_MAX	0x034

/* Queue size for the currently selected queue - Write Only */
#define VIRTIO_MMIO_QUEUE_NUM		0x038

/* Ready bit for the currently selected queue - Read Write */
#define VIRTIO_MMIO_QUEUE_READY		0x044

/* Queue notifier - Write Only */
#define VIRTIO_MMIO_QUEUE_NOTIFY	0x050

/* Interrupt status - Read Only */
#define VIRTIO_MMIO_INTERRUPT_STATUS	0x060

/* Interrupt acknowledge - Write Only */
#define VIRTIO_MMIO_INTERRUPT_ACK	0x064

/* Device status register - Read Write */
#define VIRTIO_MMIO_STATUS		0x070

/* Selected queue's Descriptor Table address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_DESC_LOW	0x080
#define VIRTIO_MMIO_QUEUE_DESC_HIGH	0x084

/* Selected queue's Available Ring address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW	0x090
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH	0x094

/* Selected queue's Used Ring address, 64 bits in two halves */
#define VIRTIO_MMIO_QUEUE_USED_LOW	0x0a0
#define VIRTIO_MMIO_QUEUE_USED_HIGH	0x0a4

/* Configuration atomicity value */
#define VIRTIO_MMIO_CONFIG_GENERATION	0x0fc

/* The config space is defined by each driver as
 * the per-driver configuration space - Read Write */
#define VIRTIO_MMIO_CONFIG		0x100

#define VIRTIO_MMIO_INT_VRING		(1 << 0)
#define VIRTIO_MMIO_INT_CONFIG		(1 << 1)

namespace virtio {

struct mmio_device_info {
    mmio_device_info(u64 address, u64 size, unsigned int irq) :
        _address(address), _size(size), _irq_no(irq) {}

    u64 _address;
    u64 _size;
    unsigned int _irq_no;
};

class mmio_device : public virtio_device {
public:
    mmio_device(mmio_device_info dev_info) :
        _dev_info(dev_info), _vendor_id(0), _device_id(0), _addr_mmio(0) {}

    virtual ~mmio_device() {}

    virtual hw_device_id get_id();

    virtual void init() {}
    virtual void print() {}
    virtual void reset() {}

    virtual unsigned get_irq() { return _dev_info._irq_no; }
    virtual u8 read_and_ack_isr();
    virtual void register_interrupt(interrupt_factory irq_factory);

    virtual void select_queue(int queue);
    virtual u16 get_queue_size();
    virtual void setup_queue(vring *queue);
    virtual void activate_queue(int queue);
    virtual void kick_queue(int queue);

    virtual u64 get_available_features();
    virtual void set_enabled_features(u64 features);

    virtual u8 get_status();
    virtual void set_status(u8 status);

    virtual u8 read_config(u32 offset);

    virtual void dump_config() {}

    virtual bool is_modern() { return true; };
    virtual size_t get_vring_alignment() { return PAGE_SIZE; }

    bool parse_config();

    virtual bool get_shm(u8 id, mmioaddr_t &addr, u64 &length) { /* TODO */ return false; }
private:
    mmio_device_info _dev_info;
    //u64 _id;
    u16 _vendor_id;
    u16 _device_id;

    mmioaddr_t _addr_mmio;
#ifdef AARCH64_PORT_STUB
    std::unique_ptr<spi_interrupt> _irq;
#else
    std::unique_ptr<gsi_edge_interrupt> _irq;
#endif
};

void parse_mmio_device_configuration(char *cmdline);
void register_mmio_devices(hw::device_manager *dev_manager);

}

#endif //VIRTIO_MMIO_DEVICE_HH
