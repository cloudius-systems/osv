#include "debug-console.hh"

debug_console::debug_console(Console& impl)
    : _impl(impl)
{
}

void debug_console::write(const char* str, size_t len)
{
    with_lock(_lock, [=] { _impl.write(str, len); });
}

void debug_console::newline()
{
    with_lock(_lock, [=] { _impl.newline(); });
}

char debug_console::readch()
{
    return with_lock(_lock, [=] { return _impl.readch(); });
 }
