
extern "C" {
#include "../../fs/vfs/prex.h"
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
console_write(struct device *dev, const void *buf, size_t *count, int blkno)
{
    console::write(reinterpret_cast<const char *>(buf), *count, false);
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
