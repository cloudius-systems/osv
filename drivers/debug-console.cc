#include "debug-console.hh"

void debug_console::set_impl(Console* impl)
{
    with_lock(_lock, [=] { _impl = impl; });
}

void debug_console::write(const char* str, size_t len)
{
    with_lock(_lock, [=] { if (_impl) { _impl->write(str, len); }});
}

void debug_console::write_ll(const char *str, size_t len)
{
    if (_impl) {
        _impl->write(str, len);
    }
}

void debug_console::newline()
{
    with_lock(_lock, [=] { if (_impl) { _impl->newline(); }});
}

char debug_console::readch()
{
    return with_lock(_lock, [=] {  return _impl ? _impl->readch() : 0; });
 }

bool debug_console::input_ready()
{
    return with_lock(_lock, [=] { return _impl && _impl->input_ready(); });
}
