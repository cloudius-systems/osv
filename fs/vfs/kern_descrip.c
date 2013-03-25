#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <osv/file.h>
#include <osv/debug.h>

/*
 * Global file descriptors table - in OSv we have a single process so file
 * descriptors are maintained globally.
 */
struct file *gfdt[FDMAX] = {0};

/*
 * Allocate a file descriptor and assign fd to it atomically.
 *
 * Grabs a reference on fp if successful.
 */
int fdalloc(struct file *fp, int *newfd)
{
	int fd;

	fhold(fp);

	for (fd = 0; fd < FDMAX; fd++) {
		if (gfdt[fd])
			continue;
		if (__sync_val_compare_and_swap(&gfdt[fd], NULL, fp) == NULL) {
			*newfd = fd;
			return 0;
		}
	}

	fdrop(fp);
	return EMFILE;
}

/*
 * Try to set assign a file pointer to a specific file descriptor.
 * Grabs a reference to the file pointer if successful.
 */
int fdset(int fd, struct file *fp)
{
	struct file *orig;

	fhold(fp);
	orig = __sync_val_compare_and_swap(&gfdt[fd], NULL, fp);
	if (orig != NULL) {
		fdrop(fp);
		return EBADF;
	}
	return 0;
}

void fdfree(int fd)
{
	struct file *fp;

	fp = __sync_lock_test_and_set(&gfdt[fd], NULL);
	assert(fp != NULL);
}

int fget(int fd, struct file **out_fp)
{
	struct file *fp;

	if (fd < 0 || fd >= FDMAX)
	        return EBADF;

	// XXX(hch): missing protection against concurrent fdtable modifications
	fp = gfdt[fd];
	if (fp == NULL)
		return EBADF;
	fhold(fp);

	*out_fp = fp;
	return 0;
}

/*
 * Allocate a file structure without installing it into the descriptor
 * table.  resultfp will have a single reference on a successful return.
 */
int falloc_noinstall(struct file **resultfp)
{
	struct file *fp;

	fp = malloc(sizeof(*fp));
	if (!fp)
		return ENOMEM;
	memset(fp, 0, sizeof(*fp));

	/* Start with a refcount of 1 */
	fp->f_count = 1;
	fp->f_ops = &badfileops;

	*resultfp = fp;
	return 0;
}

/*
 * Allocate a file structure and install it into the descriptor table.
 *
 * resultfp will have two references to it, one permanent, and one
 * to be dropped when the syscall returns.
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
	if (error)
		goto out_free_fp;

	/* Result */
	*resultfp = fp;
	*resultfd = fd;
	return 0;

out_free_fp:
	free(fp);
	return error;
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
	if (__sync_fetch_and_sub(&fp->f_count, 1))
        	return 0;

	fo_close(fp);
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
