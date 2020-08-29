/*
 * Copyright (C) 2019 Waldemar Kozaczuk.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_DEVICE_HH
#define VIRTIO_DEVICE_HH

#include <osv/types.h>
#include <osv/pci.hh>
#include <osv/interrupt.hh>
#include <osv/msi.hh>
#include "virtio-vring.hh"
#include "device.hh"

using namespace hw;

#define VIRTIO_ALIGN(x,alignment) ((x + (alignment-1)) & ~(alignment-1))

namespace virtio {

// Allows virtio drivers specify how to instantiate interrupts or
// register msi bindings. The associated virtio-device will
// use adequate functor to create correct interrupt in order
// to register it.
struct interrupt_factory {
    std::function<void(interrupt_manager &)> register_msi_bindings = nullptr;
    std::function<pci_interrupt *(pci::device &)> create_pci_interrupt = nullptr;
#ifdef AARCH64_PORT_STUB
    std::function<spi_interrupt *()> create_spi_edge_interrupt = nullptr;
#else
    std::function<gsi_edge_interrupt *()> create_gsi_edge_interrupt = nullptr;
#endif /* !AARCH64_PORT_STUB */
};

// Defines virtio transport abstraction used by virtio-driver
// to communicate with virtio device. The specializations of this
// include virtio pci device (legacy and modern) as well
// as virtio mmio device. This abstraction allows virtio driver
// not be tied to any specific transport (pci, mmio).
//
// From the specification:
// "Each device consists of the following parts:
//   - Device status field - see section 2.1 of the spec
//   - Feature bits - see section 2.2 of the spec
//   - Device Configuration space - see section 2.3 of the spec
//   - One or more virtqueues" - see section 2.4 of the spec
//
// For details please VIRTIO 1.0 specification here -
// http://docs.oasis-open.org/virtio/virtio/v1.0/virtio-v1.0.html.
class virtio_device : public hw_device {
public:
    virtual ~virtio_device() {};

    virtual void init() = 0;

    virtual unsigned get_irq() = 0;
    virtual u8 read_and_ack_isr() = 0;
    virtual void register_interrupt(interrupt_factory irq_factory) = 0;

    virtual void select_queue(int queue) = 0;
    virtual u16 get_queue_size() = 0;
    virtual void setup_queue(vring *queue) = 0;
    virtual void activate_queue(int queue) = 0;
    virtual void kick_queue(int queue) = 0;

    virtual u64 get_available_features() = 0;
    virtual void set_enabled_features(u64 features) = 0;

    // From the spec:
    // "The device status field provides a simple low-level indication of
    //  the completed steps of the device initialization sequence".
    virtual u8 get_status() = 0;
    virtual void set_status(u8 status) = 0;

    virtual u8 read_config(u32 offset) = 0;
    virtual void dump_config() = 0;

    virtual bool get_shm(u8 id, mmioaddr_t &addr, u64 &length) = 0;

    virtual bool is_modern() = 0;
    virtual size_t get_vring_alignment() = 0;
};

}

#endif //VIRTIO_DEVICE_HH
