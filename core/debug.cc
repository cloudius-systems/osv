#include <cstring>
#include <cstdarg>
#include <iostream>
#include <iomanip>
#include "boost/format.hpp"
#include "drivers/console.hh"
#include "sched.hh"
#include "debug.hh"
#include "osv/debug.h"

using namespace std;

logger* logger::_instance = nullptr;

logger::logger()
{
    this->parse_configuration();
}

logger::~logger()
{

}

bool logger::parse_configuration(void)
{
    // FIXME: read configuration from a file
    add_tag("virtio", logger_warn);
    add_tag("virtio-blk", logger_warn);
    add_tag("virtio-net", logger_warn);
    add_tag("pci", logger_info);
    add_tag("poll", logger_info);
    add_tag("dhcp", logger_info);

    return (true);
}

void logger::add_tag(const char* tag, logger_severity severity)
{
    auto it = _log_level.find(tag);
    if (it != _log_level.end()) {
        _log_level.erase(it);
    }

    _log_level.insert(make_pair(tag, severity));
}

logger_severity logger::get_tag(const char* tag)
{
    auto it = _log_level.find(tag);
    if (it == _log_level.end()) {
        return (logger_error);
    }

    return (it->second);
}

bool logger::is_filtered(const char *tag, logger_severity severity)
{
    logger_severity configured_severity = this->get_tag(tag);
    if (configured_severity == logger_none) {
        return (true);
    }

    if (severity < configured_severity) {
        return (true);
    }

    return (false);
}

const char* logger::loggable_severity(logger_severity severity)
{
    const char *ret = "-";
    switch (severity) {
    case logger_debug:
        ret = "D";
        break;
    case logger_info:
        ret = "I";
        break;
    case logger_warn:
        ret = "W";
        break;
    case logger_error:
        ret = "E";
        break;
    case logger_none:
        break;
    }

    return (ret);
}

void logger::wrt(const char* tag, logger_severity severity, const boost::format& _fmt)
{
    if (this->is_filtered(tag, severity)) {
        return;
    }

    unsigned long tid = sched::thread::current()->id();
    _lock.lock();
    debug(fmt("[%s/%d %s]: ") % loggable_severity(severity) % tid % tag);
    debug(_fmt);
    debug("\n");
    _lock.unlock();
}

void logger::wrt(const char* tag, logger_severity severity, const char* _fmt, ...)
{
    va_list ap;
    va_start(ap, _fmt);
    this->wrt(tag, severity, _fmt, ap);
    va_end(ap);
}

void logger::wrt(const char* tag, logger_severity severity, const char* _fmt, va_list ap)
{
    if (this->is_filtered(tag, severity)) {
        return;
    }

    unsigned long tid = sched::thread::current()->id();
    _lock.lock();
    kprintf("[%s/%d %s]: ", loggable_severity(severity), tid, tag);
    vkprintf(_fmt, ap);
    kprintf("\n");
    _lock.unlock();
}

extern "C" {
void tprintf(const char* tag, logger_severity severity, const char* _fmt, ...)
{
    va_list ap;
    va_start(ap, _fmt);
    logger::instance()->wrt(tag, severity, _fmt, ap);
    va_end(ap);
}
}
void debug(std::string str)
{
    console::write(str.c_str(), str.length(), false);
}

void debug(const boost::format& fmt)
{
    debug(fmt.str());
}

extern "C" {

    void debug(const char *msg)
    {
        console::write(msg, strlen(msg), false);
    }

    void debug_write(const char *msg, size_t len)
    {
        console::write(msg, len, false);
    }

    // lockless version
    void debug_ll(const char *msg)
    {
        console::write_ll(msg, strlen(msg));
    }

}
