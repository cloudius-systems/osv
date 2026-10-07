/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

// Drives the real virtio_driver against a fake transport: fake_device
// implements virtio_device with plain register fields, so the production
// constructor and feature negotiation run unmodified and every status write
// is observed.

#include "drivers/virtio.hh"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace virtio;

static unsigned checks, failures;

static void check(bool ok, const char* name)
{
    ++checks;
    if (ok) {
        std::printf("PASS: %s\n", name);
    } else {
        ++failures;
        std::printf("FAIL: %s\n", name);
    }
}

class fake_device : public virtio_device {
public:
    fake_device(bool modern, bool refuse, u64 offered)
        : _modern(modern), _refuse(refuse), _offered(offered) {}

    // Register state, inspected by the test.
    u8 _status = 0xff;
    u64 _enabled = ~0ull;
    unsigned _inits = 0;
    unsigned _features_ok_writes = 0;

    hw_device_id get_id() override { return hw_device_id(VIRTIO_VENDOR_ID, 0); }
    hw_device_type get_device_type() override { return hw_device_type::virtio_over_mmio_device; }
    void print() override {}
    void reset() override {}

    void init() override { ++_inits; }
    unsigned get_irq() override { return 0; }
    u8 read_and_ack_isr() override { return 0; }
    void register_interrupt(interrupt_factory) override {}

    void select_queue(int) override {}
    u16 get_queue_size() override { return 0; }
    void setup_queue(vring*) override {}
    void activate_queue(int) override {}
    void kick_queue(int) override {}

    u64 get_available_features() override { return _offered; }
    void set_enabled_features(u64 features) override { _enabled = features; }

    u8 get_status() override { return _status; }
    // A refusing device clears FEATURES_OK as the driver sets it (spec 3.1.1
    // step 6); everything else is stored as written.
    void set_status(u8 status) override
    {
        if (status & VIRTIO_CONFIG_S_FEATURES_OK) {
            ++_features_ok_writes;
            if (_refuse) {
                status &= ~VIRTIO_CONFIG_S_FEATURES_OK;
            }
        }
        _status = status;
    }

    u8 read_config(u32) override { return 0; }
    void write_config(u32, u8) override {}
    void dump_config() override {}
    bool get_shm(u8, mmioaddr_t&, u64&) override { return false; }
    bool is_modern() override { return _modern; }
    size_t get_vring_alignment() override { return 4096; }

private:
    bool _modern, _refuse;
    u64 _offered;
};

class fake_driver : public virtio_driver {
public:
    explicit fake_driver(virtio_device& dev) : virtio_driver(dev) {}
    std::string get_name() const override { return "tst-virtio-lifecycle"; }
    using virtio_driver::setup_features;
    using virtio_driver::setup_features_checked;
};

static const u64 ring_features = (1ull << VIRTIO_RING_F_INDIRECT_DESC) |
                                 (1ull << VIRTIO_RING_F_EVENT_IDX);
static const u8 acked = VIRTIO_CONFIG_S_ACKNOWLEDGE | VIRTIO_CONFIG_S_DRIVER;

static void test_modern_refusal()
{
    fake_device dev(true, true, ring_features | (1ull << VIRTIO_F_VERSION_1));
    fake_driver drv(dev);
    check(dev._inits == 1 && dev._status == acked, "refusal: constructor reset and acknowledged");
    int error = drv.setup_features_checked();
    check(error == ENODEV, "refusal: setup_features_checked returns ENODEV");
    check(dev._features_ok_writes == 1, "refusal: FEATURES_OK offered once");
    check(dev._enabled == ring_features, "refusal: negotiated subset was written");
    check(dev._status == acked, "refusal: status left for the caller, no failed bit set");
}

static void test_modern_accept()
{
    fake_device dev(true, false, ring_features | (1ull << VIRTIO_F_VERSION_1));
    fake_driver drv(dev);
    int error = drv.setup_features_checked();
    check(error == 0, "accept: setup_features_checked returns 0");
    check(dev._status == (acked | VIRTIO_CONFIG_S_FEATURES_OK), "accept: FEATURES_OK retained");
    check(dev._enabled == ring_features, "accept: negotiated subset was written");
    check(drv.get_indirect_buf_cap() && drv.get_event_idx_cap(), "accept: ring capabilities recorded");
}

static void test_legacy()
{
    // Legacy devices have no FEATURES_OK; a refusal flag must never be reached.
    fake_device dev(false, true, 1ull << VIRTIO_RING_F_INDIRECT_DESC);
    fake_driver drv(dev);
    int error = drv.setup_features_checked();
    check(error == 0, "legacy: setup_features_checked returns 0");
    check(dev._features_ok_writes == 0 && dev._status == acked, "legacy: FEATURES_OK not written");
    check(drv.get_indirect_buf_cap() && !drv.get_event_idx_cap(), "legacy: only offered capability recorded");
}

static void test_wrapper_accept()
{
    // The compatibility wrapper existing drivers call: same success path.
    fake_device dev(true, false, ring_features);
    fake_driver drv(dev);
    drv.setup_features();
    check(dev._status == (acked | VIRTIO_CONFIG_S_FEATURES_OK), "wrapper: setup_features accepts as before");
}

// Existing drivers keep the fail-stop wrapper: run with "wrapper-refusal" to
// prove it still aborts on refusal (the expected console line is the
// assertion, so this mode never prints SUMMARY).
static void wrapper_refusal()
{
    fake_device dev(true, true, ring_features);
    fake_driver drv(dev);
    drv.setup_features();
    std::printf("FAIL: wrapper returned after refusal\n");
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "wrapper-refusal") {
        wrapper_refusal();
        return 1;
    }
    test_modern_refusal();
    test_modern_accept();
    test_legacy();
    test_wrapper_accept();
    std::printf("SUMMARY: %u checks, %u failures\n", checks, failures);
    return failures ? 1 : 0;
}
