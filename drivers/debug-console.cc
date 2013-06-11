#include "debug-console.hh"
#include "processor.hh"

// Write to the serial port if the console is not yet initialized.  Because we
// are just dumping output, no initialization is necessary.  We take advantage
// of the fact that we are running on virtual hardware that probably does not
// implement buffering like real UART does.  If it ever becomes a problem we
// may need to improve this. But since we run only for a small amount of time
// and very early - your real console is soon to be set up, I don't anticipate
// any.
static void simple_write(const char *str, size_t len)
{
    while (len > 0) {
        processor::outb(*str++, 0x3f8);
        len--;
    }
}
void debug_console::set_impl(Console* impl)
{
    with_lock(_lock, [=] { _impl = impl; });
}

void debug_console::write(const char* str, size_t len)
{
    with_lock(_lock, [=] {
        if (_impl) {
            _impl->write(str, len);
        } else {
            simple_write(str, len);
        }
    });
}

void debug_console::write_ll(const char *str, size_t len)
{
    if (_impl) {
        _impl->write(str, len);
    } else
        simple_write(str, len);
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
