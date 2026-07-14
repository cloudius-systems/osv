/*
 * Copyright (C) 2026 Greg Burd
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "drivers/virtio-balloon.hh"

#include <osv/mmu.hh>
#include <osv/mempool.hh>
#include <osv/virt_to_phys.hh>
#include <osv/debug.hh>

#include <algorithm>
#include <vector>
#include <cstddef>

using namespace memory;

namespace virtio {

balloon::balloon(virtio_device& dev)
    : virtio_driver(dev)
    , _thread(sched::thread::make([&] { worker(); },
              sched::thread::attr().name("virtio-balloon")))
{
    setup_features();
    probe_virt_queues();

    _inflate_queue = get_virt_queue(0);
    _deflate_queue = get_virt_queue(1);

    interrupt_factory int_factory;
    int_factory.register_msi_bindings = [this](interrupt_manager &msi) {
        // Bind a vector for each queue (0 = inflate, 1 = deflate).  On a queue
        // completion, disable that queue's interrupts and wake the worker,
        // which is what wait_for_queue() is parked on during a transfer.
        msi.easy_register({
            { 0, [=] { this->_inflate_queue->disable_interrupts(); }, this->_thread.get() },
            { 1, [=] { this->_deflate_queue->disable_interrupts(); }, this->_thread.get() },
        });
    };
    int_factory.create_pci_interrupt = [this](pci::device &pci_dev) {
        return new pci_interrupt(
            pci_dev,
            [=] { return this->ack_irq(); },
            [=] { this->handle_irq(); });
    };
    _dev.register_interrupt(int_factory);

    add_dev_status(VIRTIO_CONFIG_S_DRIVER_OK);
    _thread->start();
    debugf("virtio-balloon: ready\n");
}

balloon::~balloon()
{
    WITH_LOCK(_mtx) {
        _shutdown = true;
        _worker_wake.wake_all();
    }
    if (_thread) {
        _thread->join();
    }
    // Return any still-ballooned pages to the allocator on teardown.
    WITH_LOCK(_mtx) {
        for (auto* p : _ballooned) {
            free_page(p);
        }
        _ballooned.clear();
    }
}

u64 balloon::get_driver_features()
{
    // We do not implement the stats queue; MUST_TELL_HOST and DEFLATE_ON_OOM are
    // harmless to accept and match the common host configuration.
    auto base = virtio_driver::get_driver_features();
    return base | (1ull << VIRTIO_BALLOON_F_MUST_TELL_HOST)
                | (1ull << VIRTIO_BALLOON_F_DEFLATE_ON_OOM);
}

u32 balloon::target_pages()
{
    balloon_config cfg;
    virtio_conf_read(0, &cfg, sizeof(cfg));
    return cfg.num_pages;   // little-endian; OSv targets are LE hosts
}

void balloon::update_actual(u32 actual)
{
    // Tell the host how many pages we have actually given up by writing the
    // `actual` field (offset 4 in the balloon config).  QEMU relies on this to
    // track the balloon size and to compute subsequent inflate/deflate targets.
    virtio_conf_write(offsetof(balloon_config, actual), &actual, sizeof(actual));
}

void balloon::tell_host(vring* q, u32* pfns, u32 npfns)
{
    q->init_sg();
    q->add_out_sg(pfns, npfns * sizeof(u32));
    while (!q->add_buf(pfns)) {
        while (!q->avail_ring_has_room(q->_sg_vec.size())) {
            sched::thread::wait_until([&] { return q->used_ring_can_gc(); });
            q->get_buf_gc();
        }
    }
    q->kick();
    wait_for_queue(q, &vring::used_ring_not_empty);
    u32 len;
    q->get_buf_elem(&len);
    q->get_buf_finalize();
}

void balloon::inflate(u32 count)
{
    std::vector<u32> pfns;
    std::vector<void*> pages;
    pfns.reserve(std::min(count, PFNS_PER_REQUEST));

    while (count > 0) {
        u32 batch = std::min(count, PFNS_PER_REQUEST);
        pfns.clear();
        pages.clear();
        for (u32 i = 0; i < batch; i++) {
            void* p = alloc_page();
            if (!p) {
                break;   // out of memory: give the host what we managed to get
            }
            pages.push_back(p);
            pfns.push_back(mmu::virt_to_phys(p) >> VIRTIO_BALLOON_PFN_SHIFT);
        }
        if (pfns.empty()) {
            break;
        }
        tell_host(_inflate_queue, pfns.data(), pfns.size());
        // The pages are now the host's; keep them out of the free pool by
        // holding them in _ballooned rather than freeing.
        WITH_LOCK(_mtx) {
            for (auto* p : pages) {
                _ballooned.push_back(p);
            }
        }
        count -= pfns.size();
        if (pfns.size() < batch) {
            break;   // allocation failed mid-batch; stop
        }
    }
}

void balloon::deflate(u32 count)
{
    std::vector<u32> pfns;
    std::vector<void*> pages;

    while (count > 0) {
        pfns.clear();
        pages.clear();
        WITH_LOCK(_mtx) {
            u32 batch = std::min({count, PFNS_PER_REQUEST,
                                  (u32)_ballooned.size()});
            for (u32 i = 0; i < batch; i++) {
                void* p = _ballooned.front();
                _ballooned.pop_front();
                pages.push_back(p);
                pfns.push_back(mmu::virt_to_phys(p) >> VIRTIO_BALLOON_PFN_SHIFT);
            }
        }
        if (pfns.empty()) {
            break;
        }
        tell_host(_deflate_queue, pfns.data(), pfns.size());
        // The host has released these pages; return them to the allocator.
        for (auto* p : pages) {
            free_page(p);
        }
        count -= pfns.size();
    }
}

void balloon::adjust_toward_target()
{
    u32 target = target_pages();
    u32 actual;
    WITH_LOCK(_mtx) {
        actual = _ballooned.size();
    }
    if (target > actual) {
        inflate(target - actual);
    } else if (target < actual) {
        deflate(actual - target);
    }
    WITH_LOCK(_mtx) {
        update_actual(_ballooned.size());
    }
}

void balloon::handle_irq()
{
    // Runs in interrupt context, so it must not take a sleeping lock.  The
    // worker polls the target on a timer, so a queue/interrupt notification is
    // not required for correctness; waking the worker thread is a best-effort
    // nudge to react a little sooner.
    if (_thread) {
        _thread->wake();
    }
}

bool balloon::ack_irq()
{
    return _dev.read_and_ack_isr();
}

void balloon::worker()
{
    // OSv's virtio core does not currently wire the MSI-X config-change vector,
    // so we cannot get an interrupt when the host changes num_pages.  Poll the
    // target on a modest interval instead: ballooning is not latency-critical,
    // and a sub-second reaction to memory pressure is fine.
    //
    // ponytail: polling because config-change IRQ is unwired in virtio core;
    // drop the timer and wake purely on the config-change vector once that is
    // plumbed (VIRTIO_MSI_CONFIG_VECTOR).
    using namespace std::chrono;
    WITH_LOCK(_mtx) {
        while (!_shutdown) {
            DROP_LOCK(_mtx) {
                adjust_toward_target();
            }
            if (_shutdown) {
                break;
            }
            _worker_wake.wait(&_mtx, osv::clock::uptime::now() + milliseconds(500));
        }
    }
}

hw_driver* balloon::probe(hw_device* dev)
{
    return virtio::probe<balloon, VIRTIO_ID_BALLOON>(dev);
}

}
