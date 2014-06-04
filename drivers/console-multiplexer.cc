/* Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/device.h>
#include <osv/debug.hh>
#include <signal.h>
#include "console-multiplexer.hh"

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
    for (auto driver : _drivers)
        driver->start([&] { _ldisc->read_poll(driver); });
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
            _early_driver->write(str, len);
            _early_driver->flush();
        }
    } else {
        _ldisc->write(str, len,
            [&] (const char *str, size_t len) { drivers_write(str, len); });
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

}
