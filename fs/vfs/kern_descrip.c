#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <osv/file.h>
#include <osv/poll.h>
#include <osv/debug.h>
#include <osv/mutex.h>

#include <bsd/sys/sys/queue.h>

/*
 * Global file descriptors table - in OSv we have a single process so file
 * descriptors are maintained globally.
 */
struct file *gfdt[FDMAX] = {};
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

		mutex_lock(&gfdt_lock);
		/* Now that we hold the lock,
		 * make sure the entry is still available */
		if (gfdt[fd]) {
			mutex_unlock(&gfdt_lock);
			continue;
		}

		/* Install */
		gfdt[fd] = fp;
		*newfd = fd;
		mutex_unlock(&gfdt_lock);

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

	mutex_lock(&gfdt_lock);

	fp = gfdt[fd];
	if (fp == NULL) {
		mutex_unlock(&gfdt_lock);
		return EBADF;
	}

	gfdt[fd] = NULL;
	mutex_unlock(&gfdt_lock);

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

	mutex_lock(&gfdt_lock);
	orig = gfdt[fd];
	/* Install new file structure in place */
	gfdt[fd] = fp;
	mutex_unlock(&gfdt_lock);

	if (orig)
		fdrop(orig);

	return 0;
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

	mutex_lock(&gfdt_lock);

	fp = gfdt[fd];
	if (fp == NULL) {
		mutex_unlock(&gfdt_lock);
		return EBADF;
	}

	fhold(fp);
	mutex_unlock(&gfdt_lock);

	*out_fp = fp;
	return 0;
}

/*
 * Allocate a file structure without installing it into the descriptor table.
 */
int falloc_noinstall(struct file **resultfp)
{
	struct file *fp;

	fp = malloc(sizeof(*fp));
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
	if (__sync_fetch_and_sub(&fp->f_count, 1) > 1)
		return 0;

	/* We are about to free this file structure, but we still do things with it
	 * so we increase the refcount by one, fdrop may get called again
	 * and we don't want to reach this point more than once.
	 */

	fhold(fp);
	fo_close(fp);
	poll_drain(fp);
	mutex_destroy(&fp->f_lock);
	free(fp);
	return 1;
}

int
invfo_chmod(struct file *fp, mode_t mode)
{
	return EINVAL;
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
	.fo_init	= badfo_init,
	.fo_read	= badfo_readwrite,
	.fo_write	= badfo_readwrite,
	.fo_truncate	= badfo_truncate,
	.fo_ioctl	= badfo_ioctl,
	.fo_poll	= badfo_poll,
	.fo_stat	= badfo_stat,
	.fo_close	= badfo_close,
	.fo_chmod	= badfo_chmod,
};
