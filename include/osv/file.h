/*
 * Copyright (c) 2005, Kohsuke Ohtani
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _OSV_FILE_H_
#define _OSV_FILE_H_

#ifdef _KERNEL

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <osv/mutex.h>
#include <osv/uio.h>

#include <bsd/sys/sys/queue.h>

#ifdef __cplusplus

#include <memory>
#include <vector>
#include <osv/addr_range.hh>
#include <osv/rcu.hh>
#include <osv/error.h>

#endif

__BEGIN_DECLS

/*
 * File type
 */
typedef enum {
	DTYPE_UNSPEC,
	DTYPE_VNODE,
	DTYPE_SOCKET
} filetype_t;

struct vnode;
struct file;
struct pollreq;

#define FDMAX       (0x4000)

#ifdef __cplusplus

namespace mmu {
class file_vma;
};

/*
 * File structure
 */
struct file {
	file(unsigned flags, filetype_t type, void *opaque = nullptr);
	virtual ~file();
	void operator delete(void *p) { osv::rcu_dispose(p); }

	virtual int read(struct uio *uio, int flags) = 0;
	virtual int write(struct uio *uio, int flags) = 0;
	virtual int truncate(off_t len) = 0;
	virtual int ioctl(u_long com, void *data) = 0;
	virtual int poll(int events) = 0;
	virtual int stat(struct stat* buf) = 0;
	virtual int close() = 0;
	virtual int chmod(mode_t mode) = 0;
	virtual void poll_install(pollreq& pr) {}
	virtual void poll_uninstall(pollreq& pr) {}
	virtual std::unique_ptr<mmu::file_vma> mmap(addr_range range, unsigned flags, unsigned perm, off_t offset) {
	    throw make_error(ENODEV);
	}
	virtual void* get_page(uintptr_t start, uintptr_t offset, size_t size) { throw make_error(ENOSYS);}
	virtual void put_page(void *addr, uintptr_t start, uintptr_t offset, size_t size) { throw make_error(ENOSYS);}

	int		f_flags;	/* open flags */
	int		f_count;	/* reference count, see below */
	off_t		f_offset = 0;	/* current position in file */
	struct dentry	*f_dentry = nullptr; /* dentry */
	void		*f_data;        /* file descriptor specific data */
	filetype_t	f_type;		/* descriptor type */
	TAILQ_HEAD(, poll_link) f_poll_list; /* poll request list */
	mutex_t		f_lock;		/* lock */
	std::unique_ptr<std::vector<file*>> f_epolls;
	// poll_wake_count used for implementing epoll()'s EPOLLET using poll().
	// Once we have a real epoll() implementation, it won't be needed.
	int poll_wake_count = 0;
};

// struct file above is an abstract class; subclasses need to implement 8
// methods. struct special_file defines a reasonable default implementation
// for all methods except close(), making it less verbose and error-prone to
// implement special files, which usually only need to define some of
// these operations and fail (in the standard way) on the rest.
struct special_file : public file {
    special_file(unsigned flags, filetype_t type, void *opaque = nullptr) :
        file(flags, type, opaque) { }
    virtual int read(struct uio *uio, int flags) override;
    virtual int write(struct uio *uio, int flags) override;
    virtual int truncate(off_t len) override;
    virtual int ioctl(u_long com, void *data) override;
    virtual int poll(int events) override;
    virtual int stat(struct stat* buf) override;
    virtual int chmod(mode_t mode) override;
};

#endif

// f_count rules:
//
// > 0: file is live and open, normal reference counting applies
// = 0: file is open but being removed from file table, may not
//         acquire new references
// < 0: file is being closed, may not acquire new references (but
//         close path may still call fhold()/fdrop()

#define FD_LOCK(fp)	mutex_lock(&(fp->f_lock))
#define FD_UNLOCK(fp)	mutex_unlock(&(fp->f_lock))

#define FOF_OFFSET  0x0800    /* Use the offset in uio argument */

/* Alloc an fd for fp */
int _fdalloc(struct file *fp, int *newfd, int min_fd);
int fdalloc(struct file* fp, int *newfd);
int fdset(int fd, struct file* fp);
void fdfree(int fd);
int fdclose(int fd);

filetype_t file_type(struct file *fp);
void* file_data(struct file *fp);
void file_setdata(struct file *fp, void *data);
int file_flags(struct file *fp);
struct dentry* file_dentry(struct file *fp);
off_t file_offset(struct file *fp);
void file_setoffset(struct file *fp, off_t off);
/*
 * File descriptors reference count
 */
void fhold(struct file* fp);
int fdrop(struct file* fp);

/* Get fp from fd and increment refcount */
int fget(int fd, struct file** fp);

bool is_nonblock(struct file *f);

__END_DECLS

#endif

#endif /* !_OSV_FILE_H_ */
