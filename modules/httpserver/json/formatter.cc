/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "formatter.hh"
#include "json_elements.hh"
#include <float.h>
#include <boost/math/special_functions/fpclassify.hpp>
#include <iomanip>

using namespace std;

namespace httpserver {

namespace json {

string formatter::to_json(const string& str)
{
    return '"' + json_escape_UTF8_string(str) + '"';
}

string formatter::to_json(const char* str)
{
    string res = "\"";
    res += json_escape_UTF8_string(str);
    return res + '"';
}

string formatter::to_json(int n)
{
    return to_string(n);
}

string formatter::to_json(long n)
{
    return to_string(n);
}

string formatter::to_json(float f)
{
    int inf;
    if ((inf = boost::math::isinf(f))) {
        f = inf * FLT_MAX;
    } else if (boost::math::isnan(f)) {
        f = 0;
    }
    return to_string(f);
}

string formatter::to_json(bool b)
{
    return (b) ? "true" : "false";
}

string formatter::to_json(const date_time& d)
{
    char buff[50];
    string res = "\"";
    strftime(buff, 50, TIME_FORMAT, &d);
    res += buff;
    return res + "\"";
}

string formatter::to_json(const jsonable& obj) {
    return obj.to_json();
}

std::string formatter::to_json(unsigned long l) {
    return to_string(l);
}

std::string formatter::json_escape_UTF8_string(const std::string& utf8_string) {
    std::ostringstream o;
    for (auto c = utf8_string.cbegin(); c != utf8_string.cend(); c++) {
        switch (*c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= *c && *c <= '\x1f') {
                    o << "\\u"
                      << std::hex << std::setw(4) << std::setfill('0') << (int)*c;
                } else {
                    o << *c;
                }
        }
    }
    return o.str();
}

}
}
