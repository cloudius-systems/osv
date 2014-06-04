/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */
#include "line-discipline.hh"
#include <osv/sched.hh>
#include <signal.h>

namespace console {

LineDiscipline::LineDiscipline(const termios *tio)
    : _tio(tio)
{
}

void LineDiscipline::read(struct uio *uio, int ioflag) {
    WITH_LOCK(_mutex) {
        _readers.push_back(sched::thread::current());
        sched::thread::wait_until(_mutex, [&] { return !_read_queue.empty(); });
        _readers.remove(sched::thread::current());
        while (uio->uio_resid && !_read_queue.empty()) {
            struct iovec *iov = uio->uio_iov;
            auto n = std::min(_read_queue.size(), iov->iov_len);
            for (size_t i = 0; i < n; ++i) {
                static_cast<char*>(iov->iov_base)[i] = _read_queue.front();
                _read_queue.pop();
            }

            uio->uio_resid -= n;
            uio->uio_offset += n;
            if (n == iov->iov_len) {
                uio->uio_iov++;
                uio->uio_iovcnt--;
            }
        }
    }
}

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

void LineDiscipline::read_poll(console_driver *driver)
{
    while (true) {
        std::lock_guard<mutex> lock(_mutex);
        sched::thread::wait_until(_mutex, [&] { return driver->input_ready(); });
        char c = driver->readch();
        if (c == 0)
            continue;

        if (c == '\r' && _tio->c_iflag & ICRNL) {
            c = '\n';
        }
        if (_tio->c_lflag & ISIG) {
            // Currently, INTR and QUIT signal OSv's only process, process 0.
            if (c == _tio->c_cc[VINTR]) {
                kill(0, SIGINT);
                continue;
            } else if (c == _tio->c_cc[VQUIT]) {
                kill(0, SIGQUIT);
                continue;
            }
        }

        if (_tio->c_lflag & ICANON) {
            // canonical ("cooked") mode, where input is only made
            // available to the reader after a newline (until then, the
            // user can edit it with backspace, etc.).
            if (c == '\n') {
                if (_tio->c_lflag && ECHO)
                    driver->write(&c, 1);
                _line_buffer.push_back('\n');
                while (!_line_buffer.empty()) {
                    _read_queue.push(_line_buffer.front()); _line_buffer.pop_front();
                }
                for (auto t : _readers) {
                    t->wake();
                }
                continue; // already echoed
            } else if (c == _tio->c_cc[VERASE]) {
                if (_line_buffer.empty()) {
                    continue; // do nothing, and echo nothing
                }
                char e = _line_buffer.back();
                _line_buffer.pop_back();
                if (_tio->c_lflag && ECHO) {
                    static const char eraser[] = {'\b',' ','\b','\b',' ','\b'};
                    if (_tio->c_lflag && ECHOE) {
                        if (isctrl(e)) { // Erase the two characters ^X
                            driver->write(eraser, 6);
                        } else {
                            driver->write(eraser, 3);
                        }
                    } else {
                        if (isctrl(e)) {
                            driver->write(eraser+2, 2);
                        } else {
                            driver->write(eraser, 1);
                        }
                    }
                    continue; // already echoed
                }
            } else {
                _line_buffer.push_back(c);
            }
        } else {
            // non-canonical ("cbreak") mode, where characters are made
            // available for reading as soon as they are typed.
            _read_queue.push(c);
            for (auto t : _readers) {
                t->wake();
            }
        }
        if (_tio->c_lflag & ECHO) {
            if (isctrl(c) && (_tio->c_lflag & ECHOCTL)) {
                char out[2];
                out[0] = '^';
                out[1] = c^'@';
                driver->write(out, 2);
            } else {
                driver->write(&c, 1);
            }
        }
    }
}
void LineDiscipline::write(const char *str, size_t len,
    std::function<void(const char *str, size_t len)> writer)
{
    while (len-- > 0) {
        if ((*str == '\n') &&
            (_tio->c_oflag & OPOST) && (_tio->c_oflag & ONLCR))
                writer("\r", 1);
            writer(str++, 1);
    }
}

}
