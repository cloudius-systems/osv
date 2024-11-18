/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <cstdarg>
#include <osv/printf.hh>

namespace osv {

std::string sprintf(const char* fmt...)
{
    char *output;
    va_list ap;
    va_start(ap, fmt);
    auto ret = vasprintf(&output, fmt, ap);
    va_end(ap);

    if (ret >= 0 && output) {
        auto res = std::string(output);
        free(output);
        return res;
    } else {
        return "";
    }
}

std::string vsprintf(const char* fmt, va_list ap)
{
    char *output;
    auto ret = vasprintf(&output, fmt, ap);

    if (ret >= 0 && output) {
        auto res = std::string(output);
        free(output);
        return res;
    } else {
        return "";
    }
}

}
