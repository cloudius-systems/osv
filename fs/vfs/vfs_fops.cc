/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */


#include <fcntl.h>
#include <sys/stat.h>
#include <osv/file.h>
#include <osv/poll.h>
#include <fs/vfs/vfs.h>
#include <osv/vfs_file.hh>
#include <osv/mmu.hh>

#ifndef AARCH64_PORT_STUB
#include "arch-mmu.hh"
#include <osv/pagecache.hh>
#endif /* !AARCH64_PORT_STUB */

vfs_file::vfs_file(unsigned flags)
	: file(flags, DTYPE_VNODE)
{
}

int vfs_file::close()
{
	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;
	int error;

	vn_lock(vp);
	error = VOP_CLOSE(vp, fp);
	vn_unlock(vp);

	if (error)
		return error;

	drele(fp->f_dentry);
	return 0;
}

int vfs_file::read(struct uio *uio, int flags)
{
	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;
	int error;
	size_t count;
	ssize_t bytes;

	bytes = uio->uio_resid;

	vn_lock(vp);
	if ((flags & FOF_OFFSET) == 0)
		uio->uio_offset = fp->f_offset;

	error = VOP_READ(vp, fp, uio, 0);
	if (!error) {
		count = bytes - uio->uio_resid;
		if ((flags & FOF_OFFSET) == 0)
			fp->f_offset += count;
	}
	vn_unlock(vp);

	return error;
}


int vfs_file::write(struct uio *uio, int flags)
{
	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;
	int ioflags = 0;
	int error;
	size_t count;
	ssize_t bytes;

	bytes = uio->uio_resid;

	vn_lock(vp);

	if (fp->f_flags & O_APPEND)
		ioflags |= IO_APPEND;
	if (fp->f_flags & (O_DSYNC|O_SYNC))
		ioflags |= IO_SYNC;

	if ((flags & FOF_OFFSET) == 0)
	        uio->uio_offset = fp->f_offset;

	error = VOP_WRITE(vp, uio, ioflags);
	if (!error) {
		count = bytes - uio->uio_resid;
		if ((flags & FOF_OFFSET) == 0)
			fp->f_offset += count;
	}

	vn_unlock(vp);
	return error;
}

int vfs_file::ioctl(u_long com, void *data)
{
	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;
	int error;

	vn_lock(vp);
	error = VOP_IOCTL(vp, fp, com, data);
	vn_unlock(vp);

	return error;
}

int vfs_file::stat(struct stat *st)
{
	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;
	int error;

	vn_lock(vp);
	error = vn_stat(vp, st);
	vn_unlock(vp);

	return error;
}

int vfs_file::poll(int events)
{
	return poll_no_poll(events);
}

int vfs_file::truncate(off_t len)
{
	// somehow this is handled outside file ops
	abort();
}

int vfs_file::chmod(mode_t mode)
{
	// somehow this is handled outside file ops
	abort();
}

#ifndef AARCH64_PORT_STUB
mmu::mmupage vfs_file::get_page(uintptr_t off, size_t size, mmu::hw_ptep ptep, bool write, bool shared)
{
    return pagecache::get(this, off, ptep, write, shared);
}

void vfs_file::put_page(void *addr, uintptr_t off, size_t size, mmu::hw_ptep ptep)
{
    pagecache::release(this, addr, off, ptep);
}
#endif /* !AARCH64_PORT_STUB */

// Locking: vn_lock will call into the filesystem, and that can trigger an
// eviction that will hold the mmu-side lock that protects the mappings
// Always follow that order. We however can't just get rid of the mmu-side lock,
// because not all invalidations will be synchronous.
void vfs_file::get_arcbuf(uintptr_t offset, unsigned action, void** start, size_t* len, void** page)
{
    struct vnode *vp = f_dentry->d_vnode;

    iovec io;
    io.iov_base = nullptr;
    io.iov_len = 0;

    uio data;
    data.uio_iov = &io;
    data.uio_iovcnt = 1;
    data.uio_offset = off_t(offset);
    data.uio_resid = mmu::page_size;
    data.uio_rw = UIO_READ;

    vn_lock(vp);
    assert(VOP_CACHE(vp, this, &data, action) == 0);
    vn_unlock(vp);
    *start = io.iov_base;
    *len = io.iov_len;
    *page = static_cast<char*>(io.iov_base) + data.uio_offset;
}

std::unique_ptr<mmu::file_vma> vfs_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
#ifdef AARCH64_PORT_STUB
	abort();
#else /* !AARCH64_PORT_STUB */
	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;
	if (!vp->v_op->vop_cache || (vp->v_size < (off_t)mmu::page_size)) {
		return mmu::default_file_mmap(this, range, flags, perm, offset);
	}
	return mmu::map_file_mmap(this, range, flags, perm, offset);
#endif /* !AARCH64_PORT_STUB */
}
