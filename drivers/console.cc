
#include <osv/prex.h>
#include <osv/device.h>
#include <sched.hh>
#include <queue>
#include <vector>

#include "isa-serial.hh"
#include "debug-console.hh"
#include "drivers/clock.hh"

namespace console {

// should eventually become a list of console device that we chose the best from
IsaSerialConsole serial_console;
debug_console console(serial_console);

void write(const char *msg, size_t len, bool lf)
{
    console.write(msg, len);
    if (lf)
        console.newline();
}

void read(char *msg, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        msg[i] = console.readch();
        write(&msg[i],1,false);
        if (msg[i] == '\r')
            break;
    }
}

mutex console_mutex;
std::queue<char> console_queue;
std::list<sched::thread*> readers;

// hack, until we have ISA interrupts
void console_poll()
{
    sched::timer tmr(*sched::thread::current());
    while (true) {
        while (!with_lock(console_mutex, [] { return console.input_ready(); })) {
            tmr.set(clock::get()->time() + 10_ms);
            sched::thread::wait_until([&] { return tmr.expired(); });
        }
        with_lock(console_mutex, [] {
            console_queue.push(console.readch());
            for (auto t : readers) {
                t->wake();
            }
        });
    }
}

static int
console_read(struct device *dev, struct uio *uio, int ioflag)
{
    return with_lock(console_mutex, [=] {
        readers.push_back(sched::thread::current());
        sched::thread::wait_until(console_mutex, [] { return !console_queue.empty(); });
        readers.remove(sched::thread::current());
        while (uio->uio_resid && !console_queue.empty()) {
            struct iovec *iov = uio->uio_iov;
            auto n = std::min(console_queue.size(), iov->iov_len);
            for (size_t i = 0; i < n; ++i) {
                static_cast<char*>(iov->iov_base)[i] = console_queue.front();
                console_queue.pop();
            }

            uio->uio_resid -= n;
            uio->uio_offset += n;
            if (n == iov->iov_len) {
                uio->uio_iov++;
                uio->uio_iovcnt--;
            }
        }
        return 0;
    });
}

static int
console_write(struct device *dev, struct uio *uio, int ioflag)
{
    while (uio->uio_resid > 0) {
        struct iovec *iov = uio->uio_iov;

        if (iov->iov_len) {
            with_lock(console_mutex, [=] {
                console::write(reinterpret_cast<const char *>(iov->iov_base),
                               iov->iov_len, false);
            });
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

}

using namespace console;

static struct devops console_devops = {
    .open	= no_open,
    .close	= no_close,
    .read	= console_read,
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
    (new sched::thread(console_poll))->start();
    device_create(&console_driver, "console", D_CHR);
    return 0;
}
