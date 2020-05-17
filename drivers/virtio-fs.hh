/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef VIRTIO_FS_DRIVER_H
#define VIRTIO_FS_DRIVER_H

#include <osv/mutex.h>
#include <osv/waitqueue.hh>
#include "drivers/virtio.hh"
#include "drivers/virtio-device.hh"
#include "fs/virtiofs/virtiofs_i.hh"

namespace virtio {

enum {
    VQ_HIPRIO = 0,
    VQ_REQUEST = 1
};

class fs : public virtio_driver {
public:
    struct fs_config {
        char tag[36];
        u32 num_queues;
    } __attribute__((packed));

    struct dax_window {
        mmioaddr_t addr;
        u64 len;
        mutex lock;
    };

    explicit fs(virtio_device& dev);
    virtual ~fs();

    virtual std::string get_name() const { return _driver_name; }
    void read_config();

    int make_request(fuse_request*);
    dax_window* get_dax() {
        return (_dax.addr != mmio_nullptr) ? &_dax : nullptr;
    }
    // Set map alignment for DAX window. @map_align should be
    // log2(byte_alignment), e.g. 12 for a 4096 byte alignment.
    void set_map_alignment(int map_align) { _map_align = map_align; }
    // Returns the map alignment for the DAX window as preiously set with
    // set_map_alignment(), or < 0 if it has not been set.
    int get_map_alignment() const { return _map_align; }

    void req_done();
    int64_t size();

    bool ack_irq();

    static hw_driver* probe(hw_device* dev);

private:
    struct fs_req {
        fs_req(fuse_request* f) : fuse_req(f) {};
        ~fs_req() {};

        fuse_request* fuse_req;
    };

    std::string _driver_name;
    fs_config _config;
    dax_window _dax;
    int _map_align;

    // maintains the virtio instance number for multiple drives
    static int _instance;
    int _id;
    // This mutex protects parallel make_request invocations
    mutex _lock;
};

}
#endif
