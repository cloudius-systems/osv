/* Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/device.h>
#include <osv/debug.hh>
#include <signal.h>
#include "console-multiplexer.hh"
#include <osv/kernel_config_fork.h>
#if CONF_fork
#include <osv/fork_arena.hh>
#endif

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
#if CONF_fork
    // The console/tsm screen (this->_ldisc, the tsm_screen `con` and its
    // lines[]/cells[]) is SHARED kernel infrastructure: it lives in the
    // identity mempool and is read/written from every address space (each
    // forked process writes its own stderr through it).  tsm (re)allocates
    // screen lines lazily DURING a write (scroll/scrollback/resize).  Without
    // this guard, a write issued by a fork-child APP thread routes those
    // mallocs to the COW fork arena, so the shared `con->lines[y]` ends up
    // pointing at arena memory that is COW-private to that child -- and the
    // NEXT console write from another address space dereferences a line pointer
    // that resolves to a foreign/unmapped page and faults (NULL con->lines[y]).
    // Force every allocation done under the console lock onto the identity
    // kernel heap so the tsm state is coherent across all address spaces --
    // the same discipline used for struct file, thread objects and signal
    // waiters (see fork_arena.hh).  Inert (no-op) on the non-fork path.
    fork_arena::kernel_heap_scope kh;
#endif
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
