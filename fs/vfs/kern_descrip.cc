/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <osv/file.h>
#include <osv/poll.h>
#include <osv/debug.h>
#include <osv/mutex.h>
#include <osv/rcu.hh>

#include <bsd/sys/sys/queue.h>

using namespace osv;

/*
 * Global file descriptors table - in OSv we have a single process so file
 * descriptors are maintained globally.
 */
rcu_ptr<file> gfdt[FDMAX] = {};
mutex_t gfdt_lock = MUTEX_INITIALIZER;

/*
 * Allocate a file descriptor and assign fd to it atomically.
 *
 * Grabs a reference on fp if successful.
 */
int _fdalloc(struct file *fp, int *newfd, int min_fd)
{
    int fd;

    fhold(fp);

    for (fd = min_fd; fd < FDMAX; fd++) {
        if (gfdt[fd])
            continue;

        WITH_LOCK(gfdt_lock) {
            /* Now that we hold the lock,
             * make sure the entry is still available */
            if (gfdt[fd].read_by_owner()) {
                continue;
            }

            /* Install */
            gfdt[fd].assign(fp);
            *newfd = fd;
        }

        return 0;
    }

    fdrop(fp);
    return EMFILE;
}

/*
 * Allocate a file descriptor and assign fd to it atomically.
 *
 * Grabs a reference on fp if successful.
 */
int fdalloc(struct file *fp, int *newfd)
{
    return (_fdalloc(fp, newfd, 0));
}

int fdclose(int fd)
{
    struct file* fp;

    WITH_LOCK(gfdt_lock) {

        fp = gfdt[fd].read_by_owner();
        if (fp == NULL) {
            return EBADF;
        }

        gfdt[fd].assign(nullptr);
    }

    fdrop(fp);

    return 0;
}

/*
 * Assigns a file pointer to a specific file descriptor.
 * Grabs a reference to the file pointer if successful.
 */
int fdset(int fd, struct file *fp)
{
    struct file *orig;

    if (fd < 0 || fd >= FDMAX)
        return EBADF;

    fhold(fp);

    WITH_LOCK(gfdt_lock) {
        orig = gfdt[fd].read_by_owner();
        /* Install new file structure in place */
        gfdt[fd].assign(fp);
    }

    if (orig)
        fdrop(orig);

    return 0;
}

static bool fhold_if_positive(file* f)
{
    auto c = f->f_count;
    // zero or negative f_count means that the file is being closed; don't
    // increment
    while (c > 0 && !__atomic_compare_exchange_n(&f->f_count, &c, c + 1, true,
            __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
        // nothing to do
    }
    return c > 0;
}

/*
 * Retrieves a file structure from the gfdt and increases its refcount in a
 * synchronized way, this ensures that a concurrent close will not interfere.
 */
int fget(int fd, struct file **out_fp)
{
    struct file *fp;

    if (fd < 0 || fd >= FDMAX)
        return EBADF;

    WITH_LOCK(rcu_read_lock) {
        fp = gfdt[fd].read();
        if (fp == NULL) {
            return EBADF;
        }

        if (!fhold_if_positive(fp)) {
            return EBADF;
        }
    }

    *out_fp = fp;
    return 0;
}

/*
 * Allocate a file structure without installing it into the descriptor table.
 */
int falloc_noinstall(struct file **resultfp)
{
    struct file *fp;

    fp = new file;
    if (!fp)
        return ENOMEM;
    memset(fp, 0, sizeof(*fp));

    fp->f_ops = &badfileops;
    fp->f_count = 1;
    TAILQ_INIT(&fp->f_poll_list);
    mutex_init(&fp->f_lock);

    *resultfp = fp;
    return 0;
}

/*
 * Allocate a file structure and install it into the descriptor table.
 * Holds 2 references when return successfully.
 */
int falloc(struct file **resultfp, int *resultfd)
{
    struct file *fp;
    int error;
    int fd;

    error = falloc_noinstall(&fp);
    if (error)
        return error;

    error = fdalloc(fp, &fd);
    if (error) {
        fdrop(fp);
        return error;
    }

    /* Result */
    *resultfp = fp;
    *resultfd = fd;
    return 0;
}

void finit(struct file *fp, unsigned flags, filetype_t type, void *opaque,
        struct fileops *ops)
{
    fp->f_flags = flags;
    fp->f_type = type;
    fp->f_data = opaque;
    fp->f_ops = ops;

    fo_init(fp);
}

void fhold(struct file* fp)
{
    __sync_fetch_and_add(&fp->f_count, 1);
}

int fdrop(struct file *fp)
{
    if (__sync_fetch_and_sub(&fp->f_count, 1) != 1)
        return 0;

    delete fp;
    return 1;
}

file::~file()
{
    auto fp = this;

    /* We are about to free this file structure, but we still do things with it
     * so set the refcount to INT_MIN, fhold/fdrop may get called again
     * and we don't want to reach this point more than once.
     * INT_MIN is also safe against fget() seeing this file.
     */

    fp->f_count = INT_MIN;
    fo_close(fp);
    poll_drain(fp);
}

static int
badfo_init(struct file *fp)
{
    return EBADF;
}

static int
badfo_readwrite(struct file *fp, struct uio *uio, int flags)
{
    return EBADF;
}

static int
badfo_truncate(struct file *fp, off_t length)
{
    return EINVAL;
}

static int
badfo_ioctl(struct file *fp, u_long com, void *data)
{
    return EBADF;
}

static int
badfo_poll(struct file *fp, int events)
{
    return 0;
}

static int
badfo_stat(struct file *fp, struct stat *sb)
{
    return EBADF;
}

static int
badfo_close(struct file *fp)
{
    return EBADF;
}

static int
badfo_chmod(struct file *fp, mode_t mode)
{
    return EBADF;
}

struct fileops badfileops = {
    .fo_init	 = badfo_init,
    .fo_read	 = badfo_readwrite,
    .fo_write	 = badfo_readwrite,
    .fo_truncate = badfo_truncate,
    .fo_ioctl	 = badfo_ioctl,
    .fo_poll	 = badfo_poll,
    .fo_stat	 = badfo_stat,
    .fo_close	 = badfo_close,
    .fo_chmod	 = badfo_chmod,
};
