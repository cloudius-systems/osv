/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <osv/virtio-assign.hh>
#include <drivers/virtio-net.hh>

// Currently we support only one assigned virtio device, but more could
// easily be added later.
static osv::assigned_virtio *the_assigned_virtio_device = nullptr;
namespace osv {
assigned_virtio *assigned_virtio::get() {
    return the_assigned_virtio_device;
}
}

// "impl" is a subclass of virtio::virtio_driver which also implements the
// API we export applications: osv::assigned_virtio.
class impl : public osv::assigned_virtio, public virtio::virtio_driver {
public:
    static hw_driver* probe_net(hw_device* dev);
    explicit impl(pci::device& dev)
        : virtio_driver(dev)
    {
        assert(!the_assigned_virtio_device);
        the_assigned_virtio_device = this;
    }

    virtual ~impl() {
    }

    virtual std::string get_name() const override {
        return "virtio-assigned";
    }
    virtual u32 get_driver_features() override {
        return _driver_features;
    }

    // osv::assigned_virtio API implementation:

    virtual void kick(int queue) override {
        virtio_driver::kick(queue);
    }

    virtual u32 queue_size(int queue) override
    {
        virtio_conf_writew(virtio::VIRTIO_PCI_QUEUE_SEL, queue);
        return virtio_conf_readw(virtio::VIRTIO_PCI_QUEUE_NUM);

    }
    virtual u32 init_features(u32 driver_features) override
    {
        _driver_features = driver_features;
        setup_features();
        return get_guest_features();
    }

    virtual void enable_interrupt(unsigned int queue, std::function<void(void)> handler) override
    {
        // Hack to enable_interrupt's handler in a separate thread instead of
        // directly at the interrupt context. We need this when the tries to
        // signal and eventfd, which involves a mutex and not allowed in interrupt
        // context. In the future we must get rid of this ugliess, and make the
        // handler code lock-free and allowed at interrupt context!
        _hack_threads.emplace_back(handler);
        auto *ht = &_hack_threads.back(); // assumes object won't move later
        handler = [ht] { ht->wake(); };

        // OSv's generic virtio driver has already set the device to msix, and set
        // the VIRTIO_MSI_QUEUE_VECTOR of its queue to its number.
        assert(_dev.is_msix());
        virtio_conf_writew(virtio::VIRTIO_PCI_QUEUE_SEL, queue);
        assert(virtio_conf_readw(virtio::VIRTIO_MSI_QUEUE_VECTOR) == queue);
        if (!_dev.is_msix_enabled()) {
            _dev.msix_enable();
        }
        auto vectors = _msi.request_vectors(1);
        assert(vectors.size() == 1);
        auto vec = vectors[0];
        // TODO: in _msi.easy_register() we also have code for moving the
        // interrupt's affinity to where the handling thread is. We should
        // probably do this here too.
        _msi.assign_isr(vec, handler);
        auto ok = _msi.setup_entry(queue, vec);
        assert(ok);
        vec->msix_unmask_entries();
    }

    virtual void set_queue_pfn(int queue, u64 phys) override
    {
        virtio_conf_writew(virtio::VIRTIO_PCI_QUEUE_SEL, queue);
        // Tell host about pfn
        u64 pfn = phys >> virtio::VIRTIO_PCI_QUEUE_ADDR_SHIFT;
        // A bug in virtio's design... on large memory, this can actually happen
        assert(pfn <= std::numeric_limits<u32>::max());
        virtio_conf_writel(virtio::VIRTIO_PCI_QUEUE_PFN, (u32)pfn);
    }

    virtual void set_driver_ok() override
    {
        add_dev_status(virtio::VIRTIO_CONFIG_S_DRIVER_OK);
    }

    virtual void conf_read(void *buf, int length) override
    {
        virtio_conf_read(virtio_pci_config_offset(), buf, length);
    }

private:
    u32 _driver_features;

    // This is an inefficient hack, to run enable_interrupt's handler in a
    // separate thread instead of directly at the interrupt context.
    // We need this when the tries to signal and eventfd, which involves a
    // mutex and not allowed in interrupt context. In the future we must get
    // rid of this ugliess, and make the handler code lock-free and allowed
    // at interrupt context!
    class hack_thread {
    private:
        std::atomic<bool> _stop {false};
        std::atomic<bool> _wake {false};
        std::function<void(void)> _handler;
        std::unique_ptr<sched::thread> _thread;
        void stop() {
            _stop.store(true, std::memory_order_relaxed);
            wake();
            _thread = nullptr; // join and clean up the thread
        }
        bool wait() {
            sched::thread::wait_until([&] { return _wake.load(std::memory_order_relaxed); });
            _wake.store(false, std::memory_order_relaxed);
            return _stop.load(std::memory_order_relaxed);
        }
    public:
        void wake() {
            _wake.store(true, std::memory_order_relaxed);
            _thread->wake();
        }
        explicit hack_thread(std::function<void(void)> handler)
                : _handler(handler) {
            _thread = std::unique_ptr<sched::thread>(new sched::thread([&] {
                while (!_stop.load(std::memory_order_relaxed)) {
                    wait();
                    _handler();
                }
            }));
            _thread->start();
        }
        ~hack_thread() {
            stop();
        }
    };
    std::list<hack_thread> _hack_threads;
};


namespace osv {
mmu::phys assigned_virtio::virt_to_phys(void* p)
{
    return mmu::virt_to_phys(p);
}
assigned_virtio::~assigned_virtio() {
}
}

namespace virtio {
namespace assigned {
hw_driver* probe_net(hw_device* dev)
{
    return virtio::probe<impl, virtio::net::VIRTIO_NET_DEVICE_ID>(dev);
}
}
}
