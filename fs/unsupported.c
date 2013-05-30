/* The following are functions that can be used for filling struct fileops
 * in file implementations that do not support these operations.
 */

#include <sys/errno.h>

#include "unsupported.h"

int unsupported_truncate(struct file* f, off_t length)
{
    /* ftruncate() should return EINVAL when the file descriptor does not
     * refer to a regular file.
     */
    return EINVAL;
}

int unsupported_ioctl(struct file* f, ulong comm, void* data)
{
    /* ioctl() should return the anachronisticly named ENOTTY when the request
     * does not apply to the object that the file descriptor refers to. If an
     * object does not support any ioctls, it should return this error for any
     * ioctl request.
     */
    return ENOTTY;
}

int unsupported_stat(struct file* f, struct stat* s)
{
    /* FIXME: There is no reasonable errno to return here; Posix, Linux and
     * BSD all seem to support fstat() on all kinds of file descriptors,
     * offering some partial information (such as the user id of the object's
     * owner) as much as it makes sense.
     * So this function shouldn't really exist.
     */
    return EBADF;
}

int unsupported_chmod(struct file* f, mode_t m)
{
    /* Posix specifies that EINVAL should be returned when trying to do
     * fchmod() on a pipe, which doesn't support fchmod(). By the way,
     * Linux doesn't actually fail in this case - I don't know what it does.
     */
    return EINVAL;
}

int unsupported_read(struct file *fp, struct uio *uio, int flags)
{
    /* The Linux read(2) manual page is not clear as to what to return when
     * the file is unsuitable for reading: It says that EBADF should be
     * returned when "fd is not a valid file descriptor or is not open for
     * reading" and EINVAL should be returned when "fd is attached to an
     * object which is unsuitable for reading".
     *
     * In reality, it doesn't matter what we return here - a file unsuitable
     * for reading will not have the FREAD bit, so this function will never
     * get called and instead EBADF will be returned.
     */
    return EINVAL; /* as explained above, doesn't matter what we return */
}

int unsupported_write(struct file *fp, struct uio *uio, int flags)
{
    return EINVAL; /* as explained above, doesn't matter what we return */
}
