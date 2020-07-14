/*
 * Copyright (C) 2019 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <vector>
#include <osv/debug.hh>
#include "virtio-mmio.hh"

namespace virtio {

// This implements virtio-io mmio device (transport layer, modeled after PCI).
// Read here - https://www.kraxel.org/virtio/virtio-v1.0-cs03-virtio-gpu.html#x1-1080002
hw_device_id mmio_device::get_id()
{
    return hw_device_id(0x1af4, _device_id);
}

u8 mmio_device::get_status()
{
    return mmio_getl(_addr_mmio + VIRTIO_MMIO_STATUS) & 0xff;
}

void mmio_device::set_status(u8 status)
{
    mmio_setl(_addr_mmio + VIRTIO_MMIO_STATUS, status);
}

u64 mmio_device::get_available_features()
{
    u64 features;

    mmio_setl(_addr_mmio + VIRTIO_MMIO_DEVICE_FEATURES_SEL, 1);
    features = mmio_getl(_addr_mmio + VIRTIO_MMIO_DEVICE_FEATURES);
    features <<= 32;

    mmio_setl(_addr_mmio + VIRTIO_MMIO_DEVICE_FEATURES_SEL, 0);
    features |= mmio_getl(_addr_mmio + VIRTIO_MMIO_DEVICE_FEATURES);

    return features;
}

void mmio_device::set_enabled_features(u64 features)
{
    mmio_setl(_addr_mmio + VIRTIO_MMIO_DRIVER_FEATURES_SEL, 1);
    mmio_setl(_addr_mmio + VIRTIO_MMIO_DRIVER_FEATURES, (u32)(features >> 32));

    mmio_setl(_addr_mmio + VIRTIO_MMIO_DRIVER_FEATURES_SEL, 0);
    mmio_setl(_addr_mmio + VIRTIO_MMIO_DRIVER_FEATURES, (u32)features);
}

void mmio_device::kick_queue(int queue_num)
{
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_NOTIFY, queue_num);
}

void mmio_device::select_queue(int queue_num)
{
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_SEL, queue_num);
    assert(!mmio_getl(_addr_mmio + VIRTIO_MMIO_QUEUE_READY));
}

u16 mmio_device::get_queue_size()
{
    return mmio_getl(_addr_mmio + VIRTIO_MMIO_QUEUE_NUM_MAX) & 0xffff;
}

void mmio_device::setup_queue(vring* queue)
{
    // Set size
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_NUM, queue->size());
    //
    // Pass addresses
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_DESC_LOW, (u32)queue->get_desc_addr());
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_DESC_HIGH, (u32)(queue->get_desc_addr() >> 32));

    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_AVAIL_LOW, (u32)queue->get_avail_addr());
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_AVAIL_HIGH, (u32)(queue->get_avail_addr() >> 32));

    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_USED_LOW, (u32)queue->get_used_addr());
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_USED_HIGH, (u32)(queue->get_used_addr() >> 32));
}

void mmio_device::activate_queue(int queue)
{   //
    // Make it ready
    mmio_setl(_addr_mmio + VIRTIO_MMIO_QUEUE_READY, 1 );
}

u8 mmio_device::read_and_ack_isr()
{
    unsigned long status = mmio_getl(_addr_mmio + VIRTIO_MMIO_INTERRUPT_STATUS);
    mmio_setl(_addr_mmio + VIRTIO_MMIO_INTERRUPT_ACK, status);
    return (status & VIRTIO_MMIO_INT_VRING);
}

u8 mmio_device::read_config(u32 offset)
{
    return mmio_getb(_addr_mmio + VIRTIO_MMIO_CONFIG + offset);
}

void mmio_device::register_interrupt(interrupt_factory irq_factory)
{
#ifdef AARCH64_PORT_STUB
    _irq.reset(irq_factory.create_spi_edge_interrupt());
#else
    _irq.reset(irq_factory.create_gsi_edge_interrupt());
#endif
}

bool mmio_device::parse_config()
{
    _addr_mmio = mmio_map(_dev_info._address, _dev_info._size);

    u32 magic = mmio_getl(_addr_mmio + VIRTIO_MMIO_MAGIC_VALUE);
    if (magic != ('v' | 'i' << 8 | 'r' << 16 | 't' << 24)) {
        return false;
    }

    // Check device version
    u32 version = mmio_getl(_addr_mmio + VIRTIO_MMIO_VERSION);
    if (version != 2) {
        debugf( "Version %ld not supported!\n", version);
        return false;
    }

    _device_id = mmio_getl(_addr_mmio + VIRTIO_MMIO_DEVICE_ID);
    if (_device_id == 0) {
        //
        // virtio-mmio device with an ID 0 is a (dummy) placeholder
        // with no function. End probing now with no error reported.
        debug( "Dummy virtio-mmio device detected!\n");
        return false;
    }
    _vendor_id = mmio_getl(_addr_mmio + VIRTIO_MMIO_VENDOR_ID);

    debugf("Detected virtio-mmio device: (%ld,%ld)\n", _device_id, _vendor_id);
    return true;
}

#define VIRTIO_MMIO_DEVICE_CMDLINE_PREFIX "virtio_mmio.device="
static mmio_device_info* parse_mmio_device_info(char *cmdline)
{   //
    // Parse virtio mmio device information from command line
    // appended/prepended by VMMs like firecracker. After successfully
    // parsing any found mmio device info, remove it from the commandline.
    //
    // [virtio_mmio.]device=<size>@<baseaddr>:<irq>[:<id>]
    char *prefix_pos = strstr(cmdline,VIRTIO_MMIO_DEVICE_CMDLINE_PREFIX);
    if (!prefix_pos)
        return nullptr;

    u64 size = 0;
    char *size_pos = prefix_pos + strlen(VIRTIO_MMIO_DEVICE_CMDLINE_PREFIX);
    if (sscanf(size_pos,"%ld", &size) != 1)
        return nullptr;

    char *at_pos = strstr(size_pos,"@");
    if (!at_pos)
        return nullptr;

    switch(*(at_pos - 1)) {
        case 'k':
        case 'K':
            size *= 1024;
            break;
        case 'm':
        case 'M':
            size *= (1024 * 1024);
            break;
        default:
            break;
    }

    u64 irq = 0, address = 0;
    if (sscanf(at_pos, "@%lli:%u", &address, &irq) != 2)
        return nullptr;

    // Find first white-character or null as an end of device description
    auto desc_end_pos = at_pos;
    while (*desc_end_pos != 0 && !isspace(*desc_end_pos))
        desc_end_pos++;

    // Remove conf info part from cmdline by copying over remaining part
    do {
       *prefix_pos = *desc_end_pos++;
    } while (*prefix_pos++);

    return new mmio_device_info(address, size, irq);
}

static std::vector<struct mmio_device_info> *mmio_device_info_entries = 0;

void parse_mmio_device_configuration(char *cmdline)
{   //
    // We are assuming the mmio devices information is appended to the
    // command line (at least it is the case with the firecracker) so
    // once we parse those we strip it away so only plain OSv command line is left
    mmio_device_info_entries = new std::vector<struct mmio_device_info>();
    for( auto device_info = parse_mmio_device_info(cmdline); device_info != nullptr; device_info = parse_mmio_device_info(cmdline))
        mmio_device_info_entries->push_back(*device_info);
}

void register_mmio_devices(hw::device_manager *dev_manager)
{
    for (auto info : *mmio_device_info_entries) {
        auto device = new mmio_device(info);
        if (device->parse_config()) {
            dev_manager->register_device(device);
        }
        else {
            delete device;
        }
    }
}

}
