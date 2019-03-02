/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_DRIVER_H
#define VIRTIO_DRIVER_H

#include "driver.hh"
#include "drivers/driver.hh"
#include "drivers/virtio-vring.hh"
#include "drivers/virtio-device.hh"

namespace virtio {

enum VIRTIO_CONFIG {
    /* Status byte for guest to report progress, and synchronize features. */
    /* We have seen device and processed generic fields (VIRTIO_CONFIG_F_VIRTIO) */
    VIRTIO_CONFIG_S_ACKNOWLEDGE = 1,
    /* We have found a driver for the device. */
    VIRTIO_CONFIG_S_DRIVER = 2,
    /* Driver has used its parts of the config, and is happy */
    VIRTIO_CONFIG_S_DRIVER_OK = 4,
    /* Indicates that the driver has acknowledged all the features it understands,
     * and feature negotiation is complete */
    VIRTIO_CONFIG_S_FEATURES_OK = 8,
    /* We've given up on this device. */
    VIRTIO_CONFIG_S_FAILED = 0x80,
    /* Some virtio feature bits (currently bits 28 through 31) are reserved for the
     * transport being used (eg. virtio_ring), the rest are per-device feature
     * bits. */
    VIRTIO_TRANSPORT_F_START = 28,
    VIRTIO_TRANSPORT_F_END = 32,
    /* We support indirect buffer descriptors */
    VIRTIO_RING_F_INDIRECT_DESC = 28,
    /* The Guest publishes the used index for which it expects an interrupt
     * at the end of the avail ring. Host should ignore the avail->flags field. */
    /* The Host publishes the avail index for which it expects a kick
     * at the end of the used ring. Guest should ignore the used->flags field. */
    VIRTIO_RING_F_EVENT_IDX = 29,
    /* Version bit that can be used to detect legacy vs modern devices */
    VIRTIO_F_VERSION_1 = 32,
    /* Do we get callbacks when the ring is completely used, even if we've
     * suppressed them? */
    VIRTIO_F_NOTIFY_ON_EMPTY = 24,
};

enum {
    VIRTIO_VENDOR_ID = 0x1af4,

    VIRTIO_ID_NET     = 1,
    VIRTIO_ID_BLOCK   = 2,
    VIRTIO_ID_CONSOLE = 3,
    VIRTIO_ID_RNG     = 4,
    VIRTIO_ID_BALLOON = 5,
    VIRTIO_ID_RPMSG   = 7,
    VIRTIO_ID_SCSI    = 8,
    VIRTIO_ID_9P      = 9,
    VIRTIO_ID_RPROC_SERIAL = 11,
};

const unsigned max_virtqueues_nr = 64;

class virtio_driver : public hw_driver {
public:
    explicit virtio_driver(virtio_device& dev);
    virtual ~virtio_driver();

    virtual std::string get_name() const = 0;

    virtual void dump_config();

    void probe_virt_queues();
    vring* get_virt_queue(unsigned idx);

    // block the calling thread until the queue has some used elements in it.
    void wait_for_queue(vring* queue, bool (vring::*pred)() const);

    // guest/host features physical access
    u64 get_device_features();
    void set_guest_features(u64 features);
    bool get_guest_feature_bit(int bit);

    // device status
    u8 get_dev_status();
    void set_dev_status(u8 status);
    void add_dev_status(u8 status);

    // Access virtio config space
    void virtio_conf_read(u32 offset, void* buf, int length);

    bool kick(int queue);
    void reset_device();
    void free_queues();

    bool get_indirect_buf_cap() {return _cap_indirect_buf;}
    void set_indirect_buf_cap(bool on) {_cap_indirect_buf = on;}
    bool get_event_idx_cap() {return _cap_event_idx;}
    void set_event_idx_cap(bool on) {_cap_event_idx = on;}

    size_t get_vring_alignment() { return _dev.get_vring_alignment();}

protected:
    // Actual drivers should implement this on top of the basic ring features
    virtual u32 get_driver_features() { return 1 << VIRTIO_RING_F_INDIRECT_DESC | 1 << VIRTIO_RING_F_EVENT_IDX; }
    void setup_features();
protected:
    virtio_device& _dev;
    vring* _queues[max_virtqueues_nr];
    u32 _num_queues;
    bool _cap_indirect_buf;
    bool _cap_event_idx = false;
    static int _disk_idx;
    u64 _enabled_features;
};

template <typename T, u16 ID>
hw_driver* probe(hw_device* dev)
{
    if (auto virtio_dev = dynamic_cast<virtio_device*>(dev)) {
        if (virtio_dev->get_id() == hw_device_id(VIRTIO_VENDOR_ID, ID)) {
            return new T(*virtio_dev);
        }
    }
    return nullptr;
}

}

#endif

