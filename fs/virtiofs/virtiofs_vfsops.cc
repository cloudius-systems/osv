/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <atomic>
#include <memory>
#include <mutex>
#include <new>
#include <sys/types.h>

#include <api/assert.h>
#include <osv/debug.h>
#include <osv/device.h>
#include <osv/mutex.h>

#include "drivers/virtio-fs.hh"
#include "virtiofs.hh"
#include "virtiofs_dax.hh"
#include "virtiofs_i.hh"

using fuse_request = virtio::fs::fuse_request;

static std::atomic<uint64_t> fuse_unique_id(1);

static struct {
    std::unordered_map<virtio::fs*, std::shared_ptr<virtiofs::dax_manager>,
        virtio::fs::hasher> mgrs;
    mutex lock;
} dax_managers;

int fuse_req_send_and_receive_reply(virtio::fs* drv, uint32_t opcode,
    uint64_t nodeid, void* input_args_data, size_t input_args_size,
    void* output_args_data, size_t output_args_size)
{
    std::unique_ptr<fuse_request> req {new (std::nothrow) fuse_request()};
    if (!req) {
        return ENOMEM;
    }
    req->in_header.len = sizeof(req->in_header) + input_args_size;
    req->in_header.opcode = opcode;
    req->in_header.unique = fuse_unique_id.fetch_add(1,
        std::memory_order_relaxed);
    req->in_header.nodeid = nodeid;

    req->input_args_data = input_args_data;
    req->input_args_size = input_args_size;

    req->output_args_data = output_args_data;
    req->output_args_size = output_args_size;

    assert(drv);
    drv->make_request(req.get());
    req->wait();

    int error = -req->out_header.error;

    return error;
}

void virtiofs_set_vnode(struct vnode* vnode, struct virtiofs_inode* inode)
{
    if (!vnode || !inode) {
        return;
    }

    vnode->v_data = inode;
    vnode->v_ino = inode->nodeid;

    // Set type
    if (S_ISDIR(inode->attr.mode)) {
        vnode->v_type = VDIR;
    } else if (S_ISREG(inode->attr.mode)) {
        vnode->v_type = VREG;
    } else if (S_ISLNK(inode->attr.mode)) {
        vnode->v_type = VLNK;
    }

    vnode->v_mode = 0555;
    vnode->v_size = inode->attr.size;
}

static int virtiofs_mount(struct mount* mp, const char* dev, int flags,
    const void* data)
{
    struct device* device;

    int error = device_open(dev + strlen("/dev/"), DO_RDWR, &device);
    if (error) {
        kprintf("[virtiofs] Error opening device!\n");
        return error;
    }

    std::unique_ptr<fuse_init_in> in_args {new (std::nothrow) fuse_init_in()};
    std::unique_ptr<fuse_init_out> out_args {new (std::nothrow) fuse_init_out};
    if (!in_args || !out_args) {
        return ENOMEM;
    }
    in_args->major = FUSE_KERNEL_VERSION;
    in_args->minor = FUSE_KERNEL_MINOR_VERSION;
    in_args->max_readahead = PAGE_SIZE;
    in_args->flags |= FUSE_MAP_ALIGNMENT;

    auto* drv = static_cast<virtio::fs*>(device->private_data);
    error = fuse_req_send_and_receive_reply(drv, FUSE_INIT, FUSE_ROOT_ID,
        in_args.get(), sizeof(*in_args), out_args.get(), sizeof(*out_args));
    if (error) {
        kprintf("[virtiofs] Failed to initialize fuse filesystem!\n");
        return error;
    }
    // TODO: Handle version negotiation

    virtiofs_debug("Initialized fuse filesystem with version major: %d, "
                   "minor: %d\n", out_args->major, out_args->minor);

    if (out_args->flags & FUSE_MAP_ALIGNMENT) {
        drv->set_map_alignment(out_args->map_alignment);
    }

    auto* root_node {new (std::nothrow) virtiofs_inode()};
    if (!root_node) {
        return ENOMEM;
    }
    root_node->nodeid = FUSE_ROOT_ID;
    root_node->attr.mode = S_IFDIR;

    virtiofs_set_vnode(mp->m_root->d_vnode, root_node);

    auto* m_data = new (std::nothrow) virtiofs_mount_data;
    if (!m_data) {
        return ENOMEM;
    }
    m_data->drv = drv;
    if (drv->get_dax()) {
        // The device supports the DAX window
        std::lock_guard<mutex> guard {dax_managers.lock};
        auto found = dax_managers.mgrs.find(drv);
        if (found != dax_managers.mgrs.end()) {
            // There is a dax_manager already associated with this device (the
            // device is already mounted)
            m_data->dax_mgr = found->second;
        } else {
            m_data->dax_mgr = std::make_shared<virtiofs::dax_manager>(*drv);
            if (!m_data->dax_mgr) {
                return ENOMEM;
            }
        }
    }

    mp->m_data = m_data;
    mp->m_dev = device;

    return 0;
}

static int virtiofs_sync(struct mount* mp)
{
    return 0;
}

static int virtiofs_statfs(struct mount* mp, struct statfs* statp)
{
    // TODO: Call FUSE_STATFS

    // Read only. 0 blocks free
    statp->f_bfree = 0;
    statp->f_bavail = 0;

    statp->f_ffree = 0;

    return 0;
}

static int virtiofs_unmount(struct mount* mp, int flags)
{
    auto* m_data = static_cast<virtiofs_mount_data*>(mp->m_data);
    std::lock_guard<mutex> guard {dax_managers.lock};
    if (m_data->dax_mgr && m_data->dax_mgr.use_count() == 2) {
        // This was the last mount of this device. It's safe to delete the
        // window manager.
        dax_managers.mgrs.erase(m_data->drv);
    }
    delete m_data;

    struct device* dev = mp->m_dev;
    return device_close(dev);
}

#define virtiofs_vget ((vfsop_vget_t)vfs_nullop)

struct vfsops virtiofs_vfsops = {
    virtiofs_mount,		/* mount */
    virtiofs_unmount,	/* unmount */
    virtiofs_sync,		/* sync */
    virtiofs_vget,      /* vget */
    virtiofs_statfs,	/* statfs */
    &virtiofs_vnops	    /* vnops */
};
