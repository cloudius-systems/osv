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

void* vfs_file::get_page(uintptr_t start, uintptr_t off, size_t size)
{
	assert(size == mmu::page_size);

	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;

	iovec io;
	io.iov_base = nullptr;
	io.iov_len = 0;

	uio_mapper map_data;
	uio *data = &map_data.uio;

	data->uio_iov = &io;
	data->uio_iovcnt = 1;
	data->uio_offset = off_t(off);
	// FIXME: If the buffer can hold, remap other pages as well, up to the
	// buffer size.  However, this would require heavy changes in the fill
	// and map code. Let's try it later.
	data->uio_resid = mmu::page_size;
	data->uio_rw = UIO_READ;
	map_data.buffer = nullptr;

	vn_lock(vp);
	assert(VOP_MAP(vp, fp, data) == 0);
	vn_unlock(vp);

	mmu::add_mapping(map_data.buffer, start);
	return io.iov_base + map_data.buf_off;
}

void vfs_file::put_page(void *addr, uintptr_t start, uintptr_t off, size_t size)
{
	assert(size == mmu::page_size);

	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;

	uio data;
	data.uio_iov = nullptr;
	data.uio_iovcnt = 0;
	data.uio_offset = off_t(off);
	data.uio_resid = mmu::page_size;
	data.uio_rw = UIO_READ;

	vn_lock(vp);
	assert(VOP_UNMAP(vp, fp, &data) == 0);
	vn_unlock(vp);

	mmu::remove_mapping(addr, start);
}

std::unique_ptr<mmu::file_vma> vfs_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
	auto fp = this;
	struct vnode *vp = fp->f_dentry->d_vnode;
	if ((perm & mmu::perm_write) || (!vp->v_op->vop_map)) {
		return mmu::default_file_mmap(this, range, flags, perm, offset);
	}
	// Don't know what to do if we have one but not the other
	assert(vp->v_op->vop_unmap);
	return mmu::map_file_mmap(this, range, flags, perm, offset);
}
