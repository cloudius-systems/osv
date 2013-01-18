
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
    if (uio->uio_iovcnt != 1)
        return EINVAL;
    const void *buf = uio->uio_iov->iov_base;
    size_t count = uio->uio_iov->iov_len;
    console::write(reinterpret_cast<const char *>(buf), count, false);

    uio->uio_resid -= count;
    uio->uio_offset += count;
    return 0;
}

static struct devops console_devops = {
    .open	= no_open,
    .close	= no_close,
    .read	= no_read,
    .write	= console_write,
    .ioctl	= no_ioctl,
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
