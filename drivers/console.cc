
extern "C" {
#include "../../fs/vfs/prex.h"
#include "../../fs/vfs/uio.h"
#include "../../fs/devfs/device.h"
}

#include "isa-serial.hh"

namespace console {

// should eventually become a list of console device that we chose the best from
IsaSerialConsole console;

void write(const char *msg, size_t len, bool lf)
{
    console.write(msg, len);
    if (lf)
        console.newline();
}

}

static int
console_write(struct device *dev, struct uio *uio, int ioflag)
{
    while (uio->uio_resid > 0) {
        struct iovec *iov = uio->uio_iov;

	if (iov->iov_len) {
            console::write(reinterpret_cast<const char *>(iov->iov_base),
                           iov->iov_len, false);
        }

        uio->uio_iov++;
        uio->uio_iovcnt--;
        uio->uio_resid -= iov->iov_len;
        uio->uio_offset += iov->iov_len;
    }

    return 0;
}

static int
console_ioctl(struct device *dev, u_long request, void *arg)
{
    switch (request) {
    case 0x5401: // TCGETS
        return 0;   // XXX: stubbing out to get libc into line buffering mode
    default:
        return -ENOTTY;
    }
}

static struct devops console_devops = {
    .open	= no_open,
    .close	= no_close,
    .read	= no_read,
    .write	= console_write,
    .ioctl	= console_ioctl,
    .devctl	= no_devctl,
};

struct driver console_driver = {
    .name	= "console",
    .devops	= &console_devops,
};


extern "C" int
console_init(void)
{
    device_create(&console_driver, "console", D_CHR);
    return 0;
}
