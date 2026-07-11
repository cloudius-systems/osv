/*
 * Copyright (C) 2026 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_BALLOON_DRIVER_H
#define VIRTIO_BALLOON_DRIVER_H

#include <osv/condvar.h>
#include <osv/mutex.h>

#include "drivers/virtio.hh"
#include "drivers/device.hh"

#include <deque>
#include <memory>

namespace virtio {

// virtio-balloon: lets the host reclaim guest memory on demand.  When the host
// raises the target ("num_pages" in the config), the driver allocates that many
// guest pages, hands their PFNs to the host on the inflate queue, and keeps them
// out of OSv's free pool (the memory is now the host's).  When the host lowers
// the target, the driver returns pages via the deflate queue and frees them back
// to OSv.
class balloon : public virtio_driver {
public:
    explicit balloon(virtio_device& dev);
    virtual ~balloon();

    virtual std::string get_name() const { return "virtio-balloon"; }

    static hw_driver* probe(hw_device* dev);

private:
    enum {
        VIRTIO_BALLOON_F_MUST_TELL_HOST = 0,
        VIRTIO_BALLOON_F_STATS_VQ       = 1,
        VIRTIO_BALLOON_F_DEFLATE_ON_OOM = 2,
    };

    // Config-space layout (little-endian, at offset 0).
    struct balloon_config {
        u32 num_pages;   // target: pages the host wants us to give up
        u32 actual;      // pages we have actually given up
    };

    static const unsigned VIRTIO_BALLOON_PFN_SHIFT = 12;
    // How many PFNs we push to the host per queue request.
    static const unsigned PFNS_PER_REQUEST = 256;

    void handle_irq();
    bool ack_irq();
    void worker();

    // Move the balloon toward the host's target size.
    void adjust_toward_target();
    void inflate(u32 count);   // give `count` pages to the host
    void deflate(u32 count);   // take `count` pages back from the host
    void tell_host(vring* q, u32* pfns, u32 npfns);
    u32  target_pages();
    void update_actual(u32 actual);

    virtual u64 get_driver_features() override;

    vring* _inflate_queue;
    vring* _deflate_queue;
    // Pages currently loaned to the host (their virtual addresses so we can free
    // them on deflate).  Guarded by _mtx.
    std::deque<void*> _ballooned;
    std::unique_ptr<sched::thread> _thread;
    mutex _mtx;
    condvar _worker_wake;   // wakes the worker early (shutdown / queue completion)
    bool _shutdown = false;
};

}

#endif
