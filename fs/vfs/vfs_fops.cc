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

std::unique_ptr<mmu::file_vma> vfs_file::mmap(addr_range range, unsigned flags, unsigned perm, off_t offset)
{
    return mmu::default_file_mmap(this, range, flags, perm, offset);
}
