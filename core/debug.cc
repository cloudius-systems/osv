#include <cstring>
#include <iostream>
#include <iomanip>
#include "boost/format.hpp"
#include "drivers/console.hh"
#include "debug.hh"

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
    add_tag("virtio-blk", logger_error);
    add_tag("pci", logger_info);

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

logger::logger_severity logger::get_tag(const char* tag)
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

void logger::log(const char* tag, logger_severity severity, const boost::format& _fmt)
{
    if (this->is_filtered(tag, severity)) {
        return;
    }

    debug(fmt("[%s]: ") % tag, false);
    debug(_fmt, true);
}

void logger::log(const char* tag, logger_severity severity, const char* _fmt, ...)
{
    if (this->is_filtered(tag, severity)) {
        return;
    }

    // FIXME: implement...
}

void debug(std::string str, bool lf)
{
    console::write(str.c_str(), str.length(), lf);
}

void debug(const boost::format& fmt, bool lf)
{
    debug(fmt.str(), lf);
}

extern "C" {

    void debug(const char *msg)
    {
        console::write(msg, strlen(msg), true);
    }

    void debug_write(const char *msg, size_t len)
    {
        console::write(msg, len, false);
    }

}
