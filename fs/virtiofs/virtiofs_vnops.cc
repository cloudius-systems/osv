/*
 * Copyright (C) 2020 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <sys/stat.h>
#include <dirent.h>
#include <sys/param.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include <osv/prex.h>
#include <osv/vnode.h>
#include <osv/file.h>
#include <osv/mount.h>
#include <osv/debug.h>

#include <sys/types.h>
#include <osv/device.h>
#include <osv/sched.hh>
#include <osv/mmio.hh>
#include <osv/contiguous_alloc.hh>

#include "virtiofs.hh"
#include "virtiofs_i.hh"

static constexpr uint32_t OPEN_FLAGS = O_RDONLY;

int virtiofs_init()
{
    return 0;
}

static int virtiofs_lookup(struct vnode* vnode, char* name, struct vnode** vpp)
{
    auto* inode = static_cast<virtiofs_inode*>(vnode->v_data);

    if (*name == '\0') {
        return ENOENT;
    }

    if (!S_ISDIR(inode->attr.mode)) {
        kprintf("[virtiofs] inode:%lld, ABORTED lookup of %s because not a "
                "directory\n", inode->nodeid, name);
        return ENOTDIR;
    }

    auto in_args_len = strlen(name) + 1;
    std::unique_ptr<char[]> in_args {new (std::nothrow) char[in_args_len]};
    std::unique_ptr<fuse_entry_out> out_args {
        new (std::nothrow) fuse_entry_out};
    if (!out_args || !in_args) {
        return ENOMEM;
    }
    strcpy(in_args.get(), name);

    auto* drv = static_cast<virtio::fs*>(vnode->v_mount->m_data);
    auto error = fuse_req_send_and_receive_reply(drv, FUSE_LOOKUP,
        inode->nodeid, in_args.get(), in_args_len, out_args.get(),
        sizeof(*out_args));
    if (error) {
        kprintf("[virtiofs] inode:%lld, lookup failed to find %s\n",
            inode->nodeid, name);
        // TODO: Implement proper error handling by sending FUSE_FORGET
        return error;
    }

    struct vnode* vp;
    // TODO OPT: Should we even use the cache? (consult spec on metadata)
    if (vget(vnode->v_mount, out_args->nodeid, &vp) == 1) {
        virtiofs_debug("lookup found vp in cache!\n");
        *vpp = vp;
        return 0;
    }

    auto* new_inode = new (std::nothrow) virtiofs_inode;
    if (!new_inode) {
        return ENOMEM;
    }
    new_inode->nodeid = out_args->nodeid;
    virtiofs_debug("inode %lld, lookup found inode %lld for %s!\n",
        inode->nodeid, new_inode->nodeid, name);
    memcpy(&new_inode->attr, &out_args->attr, sizeof(out_args->attr));

    virtiofs_set_vnode(vp, new_inode);
    *vpp = vp;

    return 0;
}

static int virtiofs_open(struct file* fp)
{
    if ((file_flags(fp) & FWRITE)) {
        // Do not allow opening files to write
        return EROFS;
    }

    auto* vnode = file_dentry(fp)->d_vnode;
    auto* inode = static_cast<virtiofs_inode*>(vnode->v_data);

    std::unique_ptr<fuse_open_in> in_args {new (std::nothrow) fuse_open_in()};
    std::unique_ptr<fuse_open_out> out_args {new (std::nothrow) fuse_open_out};
    if (!out_args || !in_args) {
        return ENOMEM;
    }
    in_args->flags = OPEN_FLAGS;

    auto* drv = static_cast<virtio::fs*>(vnode->v_mount->m_data);
    auto error = fuse_req_send_and_receive_reply(drv, FUSE_OPEN,
        inode->nodeid, in_args.get(), sizeof(*in_args), out_args.get(),
        sizeof(*out_args));
    if (error) {
        kprintf("[virtiofs] inode %lld, open failed\n", inode->nodeid);
        return error;
    }

    virtiofs_debug("inode %lld, opened\n", inode->nodeid);

    auto* f_data = new (std::nothrow) virtiofs_file_data;
    if (!f_data) {
        return ENOMEM;
    }
    f_data->file_handle = out_args->fh;
    // TODO OPT: Consult and possibly act upon out_args->open_flags
    file_setdata(fp, f_data);

    return 0;
}

static int virtiofs_close(struct vnode* vnode, struct file* fp)
{
    auto* inode = static_cast<virtiofs_inode*>(vnode->v_data);

    std::unique_ptr<fuse_release_in> in_args {
        new (std::nothrow) fuse_release_in()};
    if (!in_args) {
        return ENOMEM;
    }
    auto* f_data = static_cast<virtiofs_file_data*>(file_data(fp));
    in_args->fh = f_data->file_handle;
    in_args->flags = OPEN_FLAGS; // need to be same as in FUSE_OPEN

    auto* drv = static_cast<virtio::fs*>(vnode->v_mount->m_data);
    auto error = fuse_req_send_and_receive_reply(drv, FUSE_RELEASE,
        inode->nodeid, in_args.get(), sizeof(*in_args), nullptr, 0);
    if (error) {
        kprintf("[virtiofs] inode %lld, close failed\n", inode->nodeid);
        return error;
    }

    file_setdata(fp, nullptr);
    delete f_data;
    virtiofs_debug("inode %lld, closed\n", inode->nodeid);

    // TODO: Investigate if we should send FUSE_FORGET once all handles to the
    // file closed on our side

    return 0;
}

static int virtiofs_readlink(struct vnode* vnode, struct uio* uio)
{
    auto* inode = static_cast<virtiofs_inode*>(vnode->v_data);

    std::unique_ptr<char[]> link_path {new (std::nothrow) char[PATH_MAX]};
    if (!link_path) {
        return ENOMEM;
    }

    auto* drv = static_cast<virtio::fs*>(vnode->v_mount->m_data);
    auto error = fuse_req_send_and_receive_reply(drv, FUSE_READLINK,
        inode->nodeid, nullptr, 0, link_path.get(), PATH_MAX);
    if (error) {
        kprintf("[virtiofs] inode %lld, readlink failed\n", inode->nodeid);
        return error;
    }

    virtiofs_debug("inode %lld, read symlink [%s]\n", inode->nodeid,
        link_path.get());
    return uiomove(link_path.get(), strlen(link_path.get()), uio);
}

// Read @read_amt bytes from @inode, using the DAX window.
static int virtiofs_read_direct(virtiofs_inode& inode, u64 file_handle,
    u64 read_amt, virtio::fs& drv, struct uio& uio)
{
    auto* dax = drv.get_dax();
    // Enter the critical path: setup mapping -> read -> remove mapping
    std::lock_guard<mutex> guard {dax->lock};

    // Setup mapping
    // NOTE: There are restrictions on the arguments to FUSE_SETUPMAPPING, from
    // the spec: "Alignment constraints for FUSE_SETUPMAPPING and
    // FUSE_REMOVEMAPPING requests are communicated during FUSE_INIT
    // negotiation"):
    // - foffset: multiple of map_alignment from FUSE_INIT
    // - len: not larger than remaining file?
    // - moffset: multiple of map_alignment from FUSE_INIT
    // In practice, map_alignment is the host's page size, because foffset and
    // moffset are passed to mmap() on the host.
    std::unique_ptr<fuse_setupmapping_in> in_args {
        new (std::nothrow) fuse_setupmapping_in()};
    if (!in_args) {
        return ENOMEM;
    }
    in_args->fh = file_handle;
    in_args->flags = 0;
    uint64_t moffset = 0;
    in_args->moffset = moffset;

    auto map_align = drv.get_map_alignment();
    if (map_align < 0) {
        kprintf("[virtiofs] inode %lld, map alignment not set\n", inode.nodeid);
        return ENOTSUP;
    }
    uint64_t alignment = 1ul << map_align;
    auto foffset = align_down(static_cast<uint64_t>(uio.uio_offset), alignment);
    in_args->foffset = foffset;

    // The possible excess part of the file mapped due to alignment constraints
    // NOTE: map_excess <= alignemnt
    auto map_excess = uio.uio_offset - foffset;
    if (moffset + map_excess >= dax->len) {
        // No usable room in DAX window due to map_excess
        return ENOBUFS;
    }
    // Actual read amount is read_amt, or what fits in the DAX window
    auto read_amt_act = std::min<uint64_t>(read_amt,
        dax->len - moffset - map_excess);
    in_args->len = read_amt_act + map_excess;

    virtiofs_debug("inode %lld, setting up mapping (foffset=%lld, len=%lld, "
                   "moffset=%lld)\n", inode.nodeid, in_args->foffset,
                   in_args->len, in_args->moffset);
    auto error = fuse_req_send_and_receive_reply(&drv, FUSE_SETUPMAPPING,
        inode.nodeid, in_args.get(), sizeof(*in_args), nullptr, 0);
    if (error) {
        kprintf("[virtiofs] inode %lld, mapping setup failed\n", inode.nodeid);
        return error;
    }

    // Read from the DAX window
    // NOTE: It shouldn't be necessary to use the mmio* interface (i.e. volatile
    // accesses). From the spec: "Drivers map this shared memory region with
    // writeback caching as if it were regular RAM."
    // The location of the requested data in the DAX window
    auto req_data = dax->addr + moffset + map_excess;
    error = uiomove(const_cast<void*>(req_data), read_amt_act, &uio);
    if (error) {
        kprintf("[virtiofs] inode %lld, uiomove failed\n", inode.nodeid);
        return error;
    }

    // Remove mapping
    // NOTE: This is only necessary when FUSE_SETUPMAPPING fails. From the spec:
    // "If the device runs out of resources the FUSE_SETUPMAPPING request fails
    // until resources are available again following FUSE_REMOVEMAPPING."
    auto r_in_args_size = sizeof(fuse_removemapping_in) +
        sizeof(fuse_removemapping_one);
    std::unique_ptr<u8> r_in_args {new (std::nothrow) u8[r_in_args_size]};
    if (!r_in_args) {
        return ENOMEM;
    }
    auto r_in = new (r_in_args.get()) fuse_removemapping_in();
    auto r_one = new (r_in_args.get() + sizeof(fuse_removemapping_in))
        fuse_removemapping_one();
    r_in->count = 1;
    r_one->moffset = in_args->moffset;
    r_one->len = in_args->len;

    virtiofs_debug("inode %lld, removing mapping (moffset=%lld, len=%lld)\n",
        inode.nodeid, r_one->moffset, r_one->len);
    error = fuse_req_send_and_receive_reply(&drv, FUSE_REMOVEMAPPING,
        inode.nodeid, r_in_args.get(), r_in_args_size, nullptr, 0);
    if (error) {
        kprintf("[virtiofs] inode %lld, mapping removal failed\n",
            inode.nodeid);
        return error;
    }

    return 0;
}

// Read @read_amt bytes from @inode, using the fallback FUSE_READ mechanism.
static int virtiofs_read_fallback(virtiofs_inode& inode, u64 file_handle,
    u32 read_amt, u32 flags, virtio::fs& drv, struct uio& uio)
{
    std::unique_ptr<fuse_read_in> in_args {new (std::nothrow) fuse_read_in()};
    std::unique_ptr<void, std::function<void(void*)>> buf {
        memory::alloc_phys_contiguous_aligned(read_amt,
        alignof(std::max_align_t)), memory::free_phys_contiguous_aligned };
    if (!in_args | !buf) {
        return ENOMEM;
    }
    in_args->fh = file_handle;
    in_args->offset = uio.uio_offset;
    in_args->size = read_amt;
    in_args->flags = flags;

    virtiofs_debug("inode %lld, reading %lld bytes at offset %lld\n",
        inode.nodeid, read_amt, uio.uio_offset);
    auto error = fuse_req_send_and_receive_reply(&drv, FUSE_READ,
        inode.nodeid, in_args.get(), sizeof(*in_args), buf.get(), read_amt);
    if (error) {
        kprintf("[virtiofs] inode %lld, read failed\n", inode.nodeid);
        return error;
    }

    return uiomove(buf.get(), read_amt, &uio);
}

// TODO: Optimize it to reduce number of exits to host (each
// fuse_req_send_and_receive_reply()) by reading eagerly "ahead/around" just
// like ROFS does and caching it
static int virtiofs_read(struct vnode* vnode, struct file* fp, struct uio* uio,
    int ioflag)
{
    // Can't read directories
    if (vnode->v_type == VDIR) {
        return EISDIR;
    }
    // Can't read anything but reg
    if (vnode->v_type != VREG) {
        return EINVAL;
    }
    // Can't start reading before the first byte
    if (uio->uio_offset < 0) {
        return EINVAL;
    }
    // Need to read at least 1 byte
    if (uio->uio_resid == 0) {
        return 0;
    }
    // Can't read after the end of the file
    if (uio->uio_offset >= vnode->v_size) {
        return 0;
    }

    auto* inode = static_cast<virtiofs_inode*>(vnode->v_data);
    auto* file_data = static_cast<virtiofs_file_data*>(fp->f_data);
    auto* drv = static_cast<virtio::fs*>(vnode->v_mount->m_data);

    // Total read amount is what they requested, or what is left
    auto read_amt = std::min<uint64_t>(uio->uio_resid,
        inode->attr.size - uio->uio_offset);

    if (drv->get_dax()) {
        // Try to read from DAX
        if (!virtiofs_read_direct(*inode, file_data->file_handle, read_amt,
            *drv, *uio)) {

            return 0;
        }
    }
    // DAX unavailable or failed, use fallback
    return virtiofs_read_fallback(*inode, file_data->file_handle, read_amt,
        ioflag, *drv, *uio);
}

static int virtiofs_readdir(struct vnode* vnode, struct file* fp,
    struct dirent* dir)
{
    // TODO: Implement
    return EPERM;
}

static int virtiofs_getattr(struct vnode* vnode, struct vattr* attr)
{
    auto* inode = static_cast<virtiofs_inode*>(vnode->v_data);

    // TODO: Call FUSE_GETATTR? But figure out if fuse_getattr_in.fh is
    // necessary (look at the flags)
    attr->va_mode = 0555; // TODO: Is it really correct?

    if (S_ISDIR(inode->attr.mode)) {
        attr->va_type = VDIR;
    } else if (S_ISREG(inode->attr.mode)) {
        attr->va_type = VREG;
    } else if (S_ISLNK(inode->attr.mode)) {
        attr->va_type = VLNK;
    }

    attr->va_nodeid = vnode->v_ino;
    attr->va_size = inode->attr.size;

    return 0;
}

#define virtiofs_write       ((vnop_write_t)vop_erofs)
#define virtiofs_seek        ((vnop_seek_t)vop_nullop)
#define virtiofs_ioctl       ((vnop_ioctl_t)vop_nullop)
#define virtiofs_create      ((vnop_create_t)vop_erofs)
#define virtiofs_remove      ((vnop_remove_t)vop_erofs)
#define virtiofs_rename      ((vnop_rename_t)vop_erofs)
#define virtiofs_mkdir       ((vnop_mkdir_t)vop_erofs)
#define virtiofs_rmdir       ((vnop_rmdir_t)vop_erofs)
#define virtiofs_setattr     ((vnop_setattr_t)vop_erofs)
#define virtiofs_inactive    ((vnop_inactive_t)vop_nullop)
#define virtiofs_truncate    ((vnop_truncate_t)vop_erofs)
#define virtiofs_link        ((vnop_link_t)vop_erofs)
#define virtiofs_arc         ((vnop_cache_t) nullptr)
#define virtiofs_fallocate   ((vnop_fallocate_t)vop_erofs)
#define virtiofs_fsync       ((vnop_fsync_t)vop_nullop)
#define virtiofs_symlink     ((vnop_symlink_t)vop_erofs)

struct vnops virtiofs_vnops = {
    virtiofs_open,      /* open */
    virtiofs_close,     /* close */
    virtiofs_read,      /* read */
    virtiofs_write,     /* write - returns error when called */
    virtiofs_seek,      /* seek */
    virtiofs_ioctl,     /* ioctl */
    virtiofs_fsync,     /* fsync */
    virtiofs_readdir,   /* readdir */
    virtiofs_lookup,    /* lookup */
    virtiofs_create,    /* create - returns error when called */
    virtiofs_remove,    /* remove - returns error when called */
    virtiofs_rename,    /* rename - returns error when called */
    virtiofs_mkdir,     /* mkdir - returns error when called */
    virtiofs_rmdir,     /* rmdir - returns error when called */
    virtiofs_getattr,   /* getattr */
    virtiofs_setattr,   /* setattr - returns error when called */
    virtiofs_inactive,  /* inactive */
    virtiofs_truncate,  /* truncate - returns error when called */
    virtiofs_link,      /* link - returns error when called */
    virtiofs_arc,       /* arc */ //TODO: Implement to allow memory re-use when
                        // mapping files
    virtiofs_fallocate, /* fallocate - returns error when called */
    virtiofs_readlink,  /* read link */
    virtiofs_symlink    /* symbolic link - returns error when called */
};
