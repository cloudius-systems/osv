/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <cstring>
#include <cstdarg>
#include <iostream>
#include <iomanip>
#include "boost/format.hpp"
#include "drivers/console.hh"
#include <osv/sched.hh>
#include <osv/debug.hh>
#include <osv/debug.h>

#include "early-console.hh"

using namespace std;

logger* logger::_instance = nullptr;
char debug_buffer[DEBUG_BUFFER_SIZE];
int debug_buffer_idx = 0;
bool debug_buffer_full = false;
bool verbose = false;

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
    add_tag("vmxnet3", logger_warn);
    add_tag("pci", logger_info);
    add_tag("poll", logger_info);
    add_tag("dhcp", logger_info);
    add_tag("acpi", logger_error);

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
    kprintf("[%s/%lu %s]: ", loggable_severity(severity), tid, tag);
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

void fill_debug_buffer(const char *msg, size_t len)
{
    int buff_fspace;

    if (debug_buffer_idx+len < DEBUG_BUFFER_SIZE)
    {
        memcpy(&debug_buffer[debug_buffer_idx],
                msg, len);
        debug_buffer_idx += len;
    }
    else
    {
        buff_fspace = DEBUG_BUFFER_SIZE-debug_buffer_idx;
        memcpy(&debug_buffer[debug_buffer_idx], msg,
                buff_fspace);
        memcpy(&debug_buffer[0], &msg[DEBUG_BUFFER_SIZE-debug_buffer_idx],
                len-buff_fspace);
        debug_buffer_idx = len-buff_fspace;
        debug_buffer_full = true;
    }
}

void debug(std::string str)
{
    fill_debug_buffer(str.c_str(), str.length());
    if (verbose) {
        console::write(str.c_str(), str.length());
    }
}

void debug(const boost::format& fmt)
{
    debug(fmt.str());
}

void enable_verbose()
{
    verbose = true;
    flush_debug_buffer();
}

void flush_debug_buffer()
{
    if (debug_buffer_full) {
        console::write(&debug_buffer[debug_buffer_idx],
                DEBUG_BUFFER_SIZE-debug_buffer_idx);
    }
    console::write(debug_buffer, debug_buffer_idx);
}

extern "C" {

    void debugf(const char *fmt, ...)
    {
        char msg[512];

        va_list argptr;
        va_start(argptr, fmt);
        vsnprintf(msg, 512, fmt, argptr);
        va_end(argptr);

        fill_debug_buffer(msg, strlen(msg));
        if (verbose) {
            console::write(msg, strlen(msg));
        }
    }

    void debug(const char *msg)
    {
        fill_debug_buffer(msg, strlen(msg));
        if (verbose) {
            console::write(msg, strlen(msg));
        }
    }

    void debug_write(const char *msg, size_t len)
    {
        fill_debug_buffer(msg, len);
        if (verbose) {
            console::write(msg, len);
        }
    }

    // lockless version
    void debug_ll(const char *fmt, ...)
    {
        static char msg[1024];

        va_list ap;

        va_start(ap, fmt);
        vsnprintf(msg, sizeof(msg), fmt, ap);
        va_end(ap);

        console::write_ll(msg, strlen(msg));
    }

    /* Early Console Output
     *
     * For early output functionality (from boot code, premain) we need
     * to access the write() method of the arch-specific early console
     * directly, because cannot wait for the console framework to be
     * initialized, not even its "early" functionality.
     * Therefore the write() method of the early console needs
     * to work without requiring any class construction.
     */

    void debug_early(const char *msg)
    {
        console::arch_early_console.write(msg, strlen(msg));
    }

    void debug_early_u64(const char *msg, unsigned long long val)
    {
        char nr[16] = { 0 };

        for (int i = 15; i >= 0; i--) {
            unsigned char nibble = val & 0x0f;
            if (nibble <= 9) {
                nr[i] = nibble + '0';
            } else {
                nr[i] = nibble - 0x0a + 'a';
            }
            val >>= 4;
        }

        console::arch_early_console.write(msg, strlen(msg));
        console::arch_early_console.write(nr, 16);
        console::arch_early_console.write("\n", 1);
    }
}
