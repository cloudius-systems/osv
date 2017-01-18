/* Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/device.h>
#include <osv/debug.hh>
#include <signal.h>
#include "console-multiplexer.hh"

// Set with set_fp(), each console is only open on one "file" object (which
// might, in turn, be referred by multiple file descriptors).
static file *single_fp;

namespace console {

console_multiplexer::console_multiplexer(const termios *tio, console_driver *early_driver)
    : _tio(tio)
    , _early_driver(early_driver)
{
}

void console_multiplexer::driver_add(console_driver *driver)
{
    _drivers.push_back(driver);
}

void console_multiplexer::start()
{
    _ldisc = new LineDiscipline(_tio);
    _ldisc->set_fp(single_fp);
    for (auto driver : _drivers) {
        driver->start([=] { _ldisc->read_poll(driver); });
    }
    _drivers_writer =  [=](const char * str, size_t len) { this->drivers_write(str, len); };
    _started = true;
}

void console_multiplexer::read(struct uio *uio, int ioflag) {
    if (!_started)
        return;
    _ldisc->read(uio, ioflag);
}

void console_multiplexer::drivers_write(const char *str, size_t len)
{
    for (auto driver : _drivers)
        driver->write(str, len);
}

void console_multiplexer::drivers_flush()
{
    for (auto driver : _drivers)
        driver->flush();
}

void console_multiplexer::write_ll(const char *str, size_t len)
{
    if (!_started) {
        if (_early_driver != nullptr) {
            while (len-- > 0) {
                if ((*str == '\n')) {
                    _early_driver->write("\r", 1);
                }
                _early_driver->write(str++, 1);
            }
            _early_driver->flush();
        }
    } else {
        _ldisc->write(str, len, _drivers_writer);
        drivers_flush();
    }
}

void console_multiplexer::write(const char *str, size_t len)
{
    if (!_started) {
        WITH_LOCK(_early_lock) {
            write_ll(str, len);
        }
    } else {
        WITH_LOCK(_mutex) {
            write_ll(str, len);
        }
    }
}

void console_multiplexer::write(struct uio *uio, int ioflag)
{
    linearize_uio_write(uio, ioflag, [&] (const char *str, size_t len) {
        write(str, len);
    });
}

int console_multiplexer::read_queue_size()
{
    if (!_started)
        return -1;

    return _ldisc->read_queue_size();
}

void console_multiplexer::take_pending_input()
{
    if (!_started)
        return;
    _ldisc->take_pending_input();
}

void console_multiplexer::discard_pending_input()
{
    if (!_started)
        return;
    _ldisc->discard_pending_input();
}

void console_multiplexer::set_fp(file *fp)
{
    single_fp = fp;
    if (_started) {
        _ldisc->set_fp(fp);
    }
}

}
