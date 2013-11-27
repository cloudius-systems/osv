/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/virtio-rng.hh"
#include "mmu.hh"

namespace virtio {

struct virtio_rng_priv {
    virtio_rng* drv;
};

static virtio_rng_priv *to_priv(device *dev)
{
    return reinterpret_cast<virtio_rng_priv*>(dev->private_data);
}

static int
virtio_rng_read(struct device *dev, struct uio *uio, int ioflags)
{
    auto prv = to_priv(dev);

    for (auto i = 0; i < uio->uio_iovcnt; i++) {
        auto *iov = &uio->uio_iov[i];

        auto nr = prv->drv->get_random_bytes(static_cast<char*>(iov->iov_base), iov->iov_len);

        uio->uio_resid  -= nr;
        uio->uio_offset += nr;

        if (nr < iov->iov_len) {
            break;
        }
    }

    return 0;
}

static struct devops virtio_rng_devops {
    no_open,
    no_close,
    virtio_rng_read,
    no_write,
    no_ioctl,
    no_devctl,
};

struct driver virtio_rng_driver = {
    "virtio_rng",
    &virtio_rng_devops,
    sizeof(struct virtio_rng_priv),
};

virtio_rng::virtio_rng(pci::device& pci_dev)
    : virtio_driver(pci_dev)
    , _entropy(64)
    , _gsi(pci_dev.get_interrupt_line(), [&] { ack_irq(); }, [&] { handle_irq(); })
    , _thread([&] { worker(); })
{
    _queue = get_virt_queue(0);

    struct virtio_rng_priv *prv;

    _urandom_dev = device_create(&virtio_rng_driver, "urandom", D_CHR);
    prv = to_priv(_urandom_dev);
    prv->drv = this;

    _random_dev = device_create(&virtio_rng_driver, "random", D_CHR);
    prv = to_priv(_random_dev);
    prv->drv = this;

    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);

    _thread.start();
}

virtio_rng::~virtio_rng()
{
    device_destroy(_urandom_dev);
    device_destroy(_random_dev);
}

u32 virtio_rng::get_random_bytes(char *buf, u32 size)
{
    WITH_LOCK(_mtx) {
        _consumer.wait_until(_mtx, [&] {
            return _entropy_count > 0;
        });
        auto len = std::min(_entropy_count, size);
        memcpy(buf, &_entropy[0], len);
        _entropy_count -= len;
        _entropy = _entropy.shift(len);
        _producer.wake_one();
        return len;
    }
}

void virtio_rng::handle_irq()
{
    _thread.wake();
}

void virtio_rng::ack_irq()
{
    virtio_conf_readb(VIRTIO_PCI_ISR);
}

void virtio_rng::worker()
{
    for (;;) {
        WITH_LOCK(_mtx) {
            _producer.wait_until(_mtx, [&] {
                return _entropy_count < _entropy.size();
            });
            refill();
            _consumer.wake_one();
        }
    }
}

void virtio_rng::refill()
{
    void *data = &_entropy[0] + _entropy_count;
    auto paddr = mmu::virt_to_phys(data);
    auto remaining = _entropy.size() - _entropy_count;

    _queue->_sg_vec.clear();
    _queue->_sg_vec.push_back(vring::sg_node(paddr, remaining, vring_desc::VRING_DESC_F_WRITE));

    while (!_queue->add_buf(data)) {
        sched::thread::wait_until([&] {
            _queue->get_buf_gc();
            return _queue->avail_ring_has_room(_queue->_sg_vec.size());
        });
    }
    _queue->kick();

    wait_for_queue(_queue, &vring::used_ring_not_empty);

    u32 len;
    _queue->get_buf_elem(&len);
    _queue->get_buf_finalize();
    _entropy_count += len;
}

hw_driver* virtio_rng::probe(hw_device *dev)
{
    return virtio::probe<virtio_rng, VIRTIO_RNG_DEVICE_ID>(dev);
}

}
