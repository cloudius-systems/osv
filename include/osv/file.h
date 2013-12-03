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
#include <osv/rcu.hh>

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
struct fileops;
struct file;

#define FDMAX       (0x4000)

#ifdef __cplusplus


/*
 * File structure
 */
struct file {
	file(unsigned flags, filetype_t type, void *opaque,
		struct fileops *ops = nullptr);
	virtual ~file();
	void operator delete(void *p) { osv::rcu_dispose(p); }

	virtual int read(struct uio *uio, int flags);
	virtual int write(struct uio *uio, int flags);
	virtual int truncate(off_t len);
	virtual int ioctl(u_long com, void *data);
	virtual int poll(int events);
	virtual int stat(struct stat* buf);
	virtual int close();
	virtual int chmod(mode_t mode);

	int		f_flags;	/* open flags */
	int		f_count;	/* reference count, see below */
	off_t		f_offset = 0;	/* current position in file */
	struct dentry	*f_dentry = nullptr; /* dentry */
	struct fileops	*f_ops;		/* file ops abstraction */
	void		*f_data;        /* file descriptor specific data */
	filetype_t	f_type;		/* descriptor type */
	TAILQ_HEAD(, poll_link) f_poll_list; /* poll request list */
	mutex_t		f_lock;		/* lock */
	std::unique_ptr<std::vector<file*>> f_epolls;
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

typedef int fo_init_t(struct file *fp);
typedef int fo_rdwr_t(struct file *fp, struct uio *uio, int flags);
typedef int fo_truncate_t(struct file *fp, off_t length);
typedef int fo_ioctl_t(struct file *fp, u_long com, void *data);
typedef int fo_poll_t(struct file *fp, int events);
typedef int fo_stat_t(struct file *fp, struct stat *sb);
typedef int fo_close_t(struct file *fp);
typedef int fo_chmod_t(struct file *fp, mode_t mode);


struct fileops {
	fo_init_t   *fo_init;
	fo_rdwr_t   *fo_read;
	fo_rdwr_t   *fo_write;
	fo_truncate_t   *fo_truncate;
	fo_ioctl_t  *fo_ioctl;
	fo_poll_t   *fo_poll;
	fo_stat_t   *fo_stat;
	fo_close_t  *fo_close;
	fo_chmod_t  *fo_chmod;
};

extern struct fileops badfileops;
extern struct fileops vfs_ops;
extern struct fileops socketops;

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

/*
 * Easy inline functions for invoking the file operations
 */
extern fo_init_t   fo_init;
extern fo_rdwr_t   fo_read;
extern fo_rdwr_t   fo_write;
extern fo_truncate_t   fo_truncate;
extern fo_ioctl_t  fo_ioctl;
extern fo_poll_t   fo_poll;
extern fo_stat_t   fo_stat;
extern fo_close_t  fo_close;
extern fo_chmod_t  fo_chmod;

__END_DECLS

#endif

#endif /* !_OSV_FILE_H_ */
