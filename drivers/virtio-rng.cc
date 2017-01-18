/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/virtio-rng.hh"
#include "drivers/random.hh"

#include <osv/mmu.hh>
#include <algorithm>
#include <iterator>

#include <dev/random/randomdev.h>
#include <dev/random/live_entropy_sources.h>

using namespace std;

static int virtio_rng_read(void *buf, int size);

static struct random_hardware_source vrng = {
    "virtio-rng",
    RANDOM_PURE_VIRTIO,
    &virtio_rng_read,
};

static randomdev::hw_rng* s_hwrng;

// NOTE: This function is not intended to be called directly.
// Instead, it's registered as a callback into the structure used to register
// virtio-rng as a hardware source of entropy, so being called whenever needed.
static int virtio_rng_read(void *buf, int size)
{
    return s_hwrng->get_random_bytes(static_cast<char *>(buf), size);
}

namespace virtio {
rng::rng(pci::device& pci_dev)
    : virtio_driver(pci_dev)
    , _irq(pci_dev, [&] { return ack_irq(); }, [&] { handle_irq(); })
    , _thread(sched::thread::make([&] { worker(); }, sched::thread::attr().name("virtio-rng")))
{
    _queue = get_virt_queue(0);

    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    _thread->start();

    s_hwrng = this;
    live_entropy_source_register(&vrng);
}

rng::~rng()
{
    live_entropy_source_deregister(&vrng);
    s_hwrng = nullptr;
}

size_t rng::get_random_bytes(char* buf, size_t size)
{
    WITH_LOCK(_mtx) {
        // Note that _entropy.size() might be 0 if we didn't get any entropy
        // from the host, in which case we'll return 0 bytes. This is fine,
        // the caller (random_kthread()) will consume whatever entropy it
        // gets, wait a bit, and later try again.
        auto len = std::min(_entropy.size(), size);
        copy_n(_entropy.begin(), len, buf);
        _entropy.erase(_entropy.begin(), _entropy.begin() + len);
        _producer.wake_one();
        return len;
    }
}

void rng::handle_irq()
{
    _thread->wake();
}

bool rng::ack_irq()
{
    return virtio_conf_readb(VIRTIO_PCI_ISR);
}

void rng::worker()
{
    for (;;) {
        WITH_LOCK(_mtx) {
            _producer.wait_until(_mtx, [&] {
                return _entropy.size() < _pool_size;
            });
            refill();
            _consumer.wake_all();
        }
    }
}

void rng::refill()
{
    auto remaining = _pool_size - _entropy.size();
    vector<char> buf(remaining);
    u32 len;
    DROP_LOCK(_mtx) {
        void* data = buf.data();

        _queue->init_sg();
        _queue->add_in_sg(data, remaining);

        while (!_queue->add_buf(data)) {
            while (!_queue->avail_ring_has_room(_queue->_sg_vec.size())) {
                sched::thread::wait_until([&] {return _queue->used_ring_can_gc();});
                _queue->get_buf_gc();
            }
        }
        _queue->kick();

        wait_for_queue(_queue, &vring::used_ring_not_empty);

        _queue->get_buf_elem(&len);
        _queue->get_buf_finalize();
    }
    copy_n(buf.begin(), len, back_inserter(_entropy));
}

hw_driver* rng::probe(hw_device* dev)
{
    return virtio::probe<rng, VIRTIO_RNG_DEVICE_ID>(dev);
}

}
