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
#include <api/poll.h>

#include <osv/mutex.h>
#include <osv/uio.h>

#include <bsd/sys/sys/queue.h>
#include <osv/dentry.h>

#ifdef __cplusplus

#include <memory>
#include <vector>
#include <osv/addr_range.hh>
#include <osv/rcu.hh>
#include <osv/error.h>
#include <osv/clock.hh>
#include <boost/optional/optional.hpp>
#include <osv/mmu-defs.hh>
#include <boost/intrusive/list.hpp>

#endif

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

/* linked list of pollreq links */
struct poll_link {
    boost::intrusive::list_member_hook<> _link;
    struct pollreq* _req = nullptr;
    /* Events being polled... */
    int _events = 0;
};

struct file;
struct epoll_key {
    int _fd;
    file* _file;
};

inline bool operator==(const epoll_key& k1, const epoll_key& k2) {
    return k1._fd == k2._fd && k1._file == k2._file;
}

namespace std {

template <>
struct hash<epoll_key> : private hash<int>, hash<file*> {
    size_t operator()(const epoll_key& key) const {
        return hash<int>::operator()(key._fd)
            ^ hash<file*>::operator()(key._file);
    }
};

}

struct epoll_file;

struct epoll_ptr {
    epoll_file* epoll;
    epoll_key key;
};

void epoll_wake(const epoll_ptr& ep);
void epoll_wake_in_rcu(const epoll_ptr& ep);

inline bool operator==(const epoll_ptr& p1, const epoll_ptr& p2) {
    return p1.epoll == p2.epoll && p1.key == p2.key;
}

namespace std {

template <>
struct hash<epoll_ptr> : private hash<epoll_file*>, private hash<epoll_key> {
    size_t operator()(const epoll_ptr& p) const {
        return hash<epoll_file*>::operator()(p.epoll)
            ^ hash<epoll_key>::operator()(p.key);
    }
};

}

/*
 * File structure
 */
struct file {
	using clock = osv::clock::uptime;
	using timeout_t = boost::optional<clock::time_point>;

	static int poll_many(struct pollfd pfd[], nfds_t nfds, timeout_t timeout);

	file(unsigned flags, filetype_t type, void *opaque = nullptr);
	virtual ~file();

	virtual int read(struct uio *uio, int flags) = 0;
	virtual int write(struct uio *uio, int flags) = 0;
	virtual int truncate(off_t len) = 0;
	virtual int ioctl(u_long com, void *data) = 0;
	virtual int poll(int events) = 0;
	virtual int poll_sync(struct pollfd& pfd, timeout_t timeout) {
		return poll_many(&pfd, 1, timeout);
	}
	virtual int stat(struct stat* buf) = 0;
	virtual int close() = 0;
	virtual int chmod(mode_t mode) = 0;
	virtual void epoll_add() {}
	virtual void epoll_del() {}
	virtual void poll_install(pollreq& pr) {}
	virtual void poll_uninstall(pollreq& pr) {}
	virtual std::unique_ptr<mmu::file_vma> mmap(addr_range range, unsigned flags, unsigned perm, off_t offset) {
	    throw make_error(ENODEV);
	}
	virtual bool map_page(uintptr_t offset, mmu::hw_ptep<0> ptep, mmu::pt_element<0> pte, bool write, bool shared) { throw make_error(ENOSYS); }
	virtual bool map_page(uintptr_t offset, mmu::hw_ptep<1> ptep, mmu::pt_element<1> pte, bool write, bool shared) { throw make_error(ENOSYS); }
	virtual bool put_page(void *addr, uintptr_t offset, mmu::hw_ptep<0> ptep) { throw make_error(ENOSYS); }
	virtual bool put_page(void *addr, uintptr_t offset, mmu::hw_ptep<1> ptep) { throw make_error(ENOSYS); }
	virtual void sync(off_t start, off_t end) { throw make_error(ENOSYS); }

	int		f_flags;	/* open flags */
	int		f_count;	/* reference count, see below */
	off_t		f_offset = 0;	/* current position in file */
	dentry_ref	f_dentry;	/* dentry */
	void		*f_data;        /* file descriptor specific data */
	filetype_t	f_type;		/* descriptor type */
	boost::intrusive::list<poll_link,
	                       boost::intrusive::member_hook<poll_link,
	                                                     boost::intrusive::list_member_hook<>,
	                                                     &poll_link::_link>,
	                       boost::intrusive::constant_time_size<false>>
	                f_poll_list; /* poll request list */
	mutex_t		f_lock;		/* lock */
	std::unique_ptr<std::vector<epoll_ptr>> f_epolls;
	// poll_wake_count used for implementing epoll()'s EPOLLET using poll().
	// Once we have a real epoll() implementation, it won't be needed.
	int poll_wake_count = 0;
	void stop_polls();
	void wake_epoll(int possible_events = -1);
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

struct tty_file : public special_file {
    tty_file(unsigned flags, filetype_t type) :
        special_file(flags, type) { }
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

__BEGIN_DECLS

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

__END_DECLS

/* Get fp from fd and increment refcount */
int fget(int fd, struct file** fp);

bool is_nonblock(struct file *f);

#endif

#endif /* !_OSV_FILE_H_ */
