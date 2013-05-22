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

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <osv/mutex.h>
#include <osv/poll.h>
#include <osv/uio.h>

#include <bsd/sys/sys/queue.h>

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

#define FDMAX       (0x4000)

/*
 * File structure
 */
struct file {
	int		f_flags;	/* open flags */
	int		f_count;	/* reference count */
	off_t		f_offset;	/* current position in file */
	struct vnode	*f_vnode;	/* vnode */
	struct fileops	*f_ops;		/* file ops abstraction */
	void		*f_data;	/* file descriptor specific data */
	filetype_t	f_type;		/* descriptor type */
	TAILQ_HEAD(, poll_link) f_poll_list; /* poll request list */
	mutex_t		f_lock;		/* lock */
};

#define FD_LOCK(fp)	mutex_lock(&(fp->f_lock))
#define FD_UNLOCK(fp)	mutex_unlock(&(fp->f_lock))

typedef struct file *file_t;

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

fo_chmod_t  invfo_chmod;

/* Alloc an fd for fp */
int _fdalloc(struct file *fp, int *newfd, int min_fd);
int fdalloc(struct file* fp, int *newfd);
int fdset(int fd, struct file* fp);
void fdfree(int fd);
int fdclose(int fd);

/*
 * File descriptors reference count
 */
void fhold(struct file* fp);
int fdrop(struct file* fp);

/* Get fp from fd and increment refcount */
int fget(int fd, struct file** fp);

/* Allocate and initialize a file descriptor */
int falloc_noinstall(struct file **resultfp);
int falloc(struct file **resultfp, int *resultfd);
void finit(struct file *fp, unsigned flags, filetype_t type,
    void *opaque, struct fileops *ops);


/*
 * Easy inline functions for invoking the file operations
 */
static __inline fo_init_t   fo_init;
static __inline fo_rdwr_t   fo_read;
static __inline fo_rdwr_t   fo_write;
static __inline fo_truncate_t   fo_truncate;
static __inline fo_ioctl_t  fo_ioctl;
static __inline fo_poll_t   fo_poll;
static __inline fo_stat_t   fo_stat;
static __inline fo_close_t  fo_close;
static __inline fo_chmod_t  fo_chmod;

static __inline int
fo_init(struct file *fp)
{
	return fp->f_ops->fo_init(fp);
}

static __inline int
fo_read(struct file *fp, struct uio *uio, int flags)
{
	return fp->f_ops->fo_read(fp, uio, flags);
}

static __inline int
fo_write(struct file *fp, struct uio *uio, int flags)
{
	return fp->f_ops->fo_write(fp, uio, flags);
}

static __inline int
fo_truncate(struct file *fp, off_t length)
{
	return fp->f_ops->fo_truncate(fp, length);
}

static __inline int
fo_ioctl(struct file *fp, u_long com, void *data)
{
	return fp->f_ops->fo_ioctl(fp, com, data);
}

static __inline int
fo_poll(struct file *fp, int events)
{
	return fp->f_ops->fo_poll(fp, events);
}

static __inline int
fo_stat(struct file *fp, struct stat *sb)
{
	return fp->f_ops->fo_stat(fp, sb);
}

static __inline int
fo_close(struct file *fp)
{
	return fp->f_ops->fo_close(fp);
}

static __inline int
fo_chmod(struct file *fp, mode_t mode)
{
	return fp->f_ops->fo_chmod(fp, mode);
}

__END_DECLS

#endif /* !_OSV_FILE_H_ */
