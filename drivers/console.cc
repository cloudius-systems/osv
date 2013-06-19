
#include <osv/prex.h>
#include <osv/device.h>
#include <sched.hh>
#include <queue>
#include <deque>
#include <vector>
#include <sys/ioctl.h>

#include "isa-serial.hh"
#include "debug-console.hh"
#include "drivers/clock.hh"
#include <termios.h>

#include <fs/fs.hh>
#include <fs/unsupported.h>

namespace console {

// should eventually become a list of console device that we chose the best from
debug_console console;

void write(const char *msg, size_t len, bool lf)
{
    console.write(msg, len);
    if (lf)
        console.newline();
}

// lockless version
void write_ll(const char *msg, size_t len)
{
    console.write_ll(msg, len);
}

mutex console_mutex;
// characters available to be returned on read() from the console
std::queue<char> console_queue;
// who to wake when characters are added to console_queue
std::list<sched::thread*> readers;

termios tio = {
    .c_iflag = ICRNL,
    .c_oflag = 0,
    .c_cflag = 0,
    .c_lflag = ECHO | ECHOCTL | ICANON | ECHOE,
    .c_line = 0,
    .c_cc = {/*VINTR*/0, /*VQUIT*/0, /*VERASE*/'\177', /*VKILL*/0,
            /*VEOF*/0, /*VTIME*/0, /*VMIN*/0, /*VSWTC*/0,
            /*VSTART*/0, /*VSTOP*/0, /*VSUSP*/0, /*VEOL*/0,
            /*VREPRINT*/0, /*VDISCARD*/0, /*VWERASE*/0,
            /*VLNEXT*/0, /*VEOL2*/0},
};

// Console line discipline thread.
//
// The "line discipline" is an intermediate layer between a physical device
// (here a serial port) and a character-device interface (here console_read())
// implementing features such as input echo, line editing, etc. In OSv, this
// is implemented in a thread, which is also responsible for read-ahead (input
// characters are read, echoed and buffered even if no-one is yet reading).
//
// The code below implements a fixed line discipline (actually two - canonical
// and non-canonical). We resisted the temptation to make the line discipline
// a stand-alone pluggable object: In the early 1980s, 8th Edition Research
// Unix experimented with pluggable line disciplines, providing improved
// editing features such as CRT erase (backspace outputs backspace-space-
// backspace), word erase, etc. These pluggable line-disciplines led to the
// development of Unix "STREAMS". However, today, these concepts are all but
// considered obsolete: In the mid 80s it was realized that these editing
// features can better be implemented in userspace code - Contemporary shells
// introduced sophisticated command-line editing (tcsh and ksh were both
// announced in 1983), and the line-editing libraries appeared (GNU Readline,
// in 1989). Posix's standardization of termios(3) also more-or-less set in
// stone the features that Posix-compliant line discipline should support.
//
// We currently support only a subset of the termios(3) features, which we
// considered most useful. More of the features can be added as needed.

static inline bool isctrl(char c) {
    return ((c<' ' && c!='\t' && c!='\n') || c=='\177');
}
// inputed characters not yet made available to read() in ICANON mode
std::deque<char> line_buffer;

void console_poll()
{
    while (true) {
        std::lock_guard<mutex> lock(console_mutex);
        sched::thread::wait_until(console_mutex, [&] { return console.input_ready(); });
        char c = console.readch();
        if (c == '\r' && tio.c_iflag & ICRNL) {
            c = '\n';
        }
        if (tio.c_lflag & ICANON) {
            // canonical ("cooked") mode, where input is only made
            // available to the reader after a newline (until then, the
            // user can edit it with backspace, etc.).
            if (c == '\n') {
                if (tio.c_lflag && ECHO)
                    console.write(&c, 1);
                line_buffer.push_back('\n');
                while (!line_buffer.empty()) {
                    console_queue.push(line_buffer.front());
                    line_buffer.pop_front();
                }
                for (auto t : readers) {
                    t->wake();
                }
                continue; // already echoed
            } else if (c == tio.c_cc[VERASE]) {
                if (line_buffer.empty()) {
                    continue; // do nothing, and echo nothing
                }
                char e = line_buffer.back();
                line_buffer.pop_back();
                if (tio.c_lflag && ECHO) {
                    static const char eraser[] = {'\b',' ','\b','\b',' ','\b'};
                    if (tio.c_lflag && ECHOE) {
                        if (isctrl(e)) { // Erase the two characters ^X
                            console.write(eraser, 6);
                        } else {
                            console.write(eraser, 3);
                        }
                    } else {
                        if (isctrl(e)) {
                            console.write(eraser+2, 2);
                        } else {
                            console.write(eraser, 1);
                        }
                    }
                    continue; // already echoed
                }
            } else {
                line_buffer.push_back(c);
            }
        } else {
            // non-canonical ("cbreak") mode, where characters are made
            // available for reading as soon as they are typed.
            console_queue.push(c);
            for (auto t : readers) {
                t->wake();
            }
        }
        if (tio.c_lflag & ECHO) {
            if (isctrl(c) && (tio.c_lflag & ECHOCTL)) {
                char out[2];
                out[0] = '^';
                out[1] = c^'@';
                console.write(out, 2);
            } else {
                console.write(&c, 1);
            }
        }
    }
}

static int
console_read(struct uio *uio, int ioflag)
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
console_write(struct uio *uio, int ioflag)
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
console_ioctl(u_long request, void *arg)
{
    switch (request) {
    case TCGETS:
        *static_cast<termios*>(arg) = tio;
        return 0;
    case TCSETS:
        tio = *static_cast<termios*>(arg);
        return 0;
    default:
        return -ENOTTY;
    }
}

template <class T> static int
op_read(T *ignore, struct uio *uio, int ioflag)
{
    return console_read(uio, ioflag);
}

template <class T> static int
op_write(T *ignore, struct uio *uio, int ioflag)
{
    return console_write(uio, ioflag);
}

template <class T> static int
op_ioctl(T *ignore, u_long request, void *arg)
{
    return console_ioctl(request, arg);
}

static struct devops console_devops = {
    .open	= no_open,
    .close	= no_close,
    .read	= op_read<device>,
    .write	= op_write<device>,
    .ioctl	= op_ioctl<device>,
    .devctl	= no_devctl,
};

struct driver console_driver = {
    .name	= "console",
    .devops	= &console_devops,
};

void console_init(void)
{
    auto console_poll_thread = new sched::thread(console_poll);
    Console* serial_console = new IsaSerialConsole(console_poll_thread);
    console_poll_thread->start();
    console::console.set_impl(serial_console);
    device_create(&console_driver, "console", D_CHR);
}

// The above allows opening /dev/console to use the console. We currently
// have a bug with the VFS/device layer - vfs_read() and vfs_write() hold a
// lock throughout the call, preventing a write() to the console while read()
// is blocked. Moreover, poll() can't be implemented on files, including
// character special devices.
// To bypass this buggy layer, here's an console::open() function, a
// replacement for open("/dev/console", O_RDWR, 0).
static int fops_console_init_close(file *f)
{
    return 0;
}

static int
fops_console_stat(file *f, struct stat *s)
{
    // We are not expected to fill much in s. Java expects (see os::available
    // in os_linux.cpp) S_ISCHR to be true.
    // TODO: what does Linux fill when we stat() a tty?
    memset(s, 0, sizeof(struct stat));
    s->st_mode = S_IFCHR;
    return 0;
}


static fileops console_ops = {
    fops_console_init_close,
    op_read<file>,
    op_write<file>,
    unsupported_truncate,
    op_ioctl<file>,
    unsupported_poll, // FIXME: implement this, and don't forget poll_wake()
    fops_console_stat,
    fops_console_init_close,
    unsupported_chmod,
};

int open() {
    try {
        fileref f{falloc_noinstall()};
        finit(f.get(), FREAD|FWRITE, DTYPE_UNSPEC, NULL, &console_ops);
        fdesc fd(f);
        return fd.release();
    } catch (int error) {
        errno = error;
        return -1;
    }
}

}
