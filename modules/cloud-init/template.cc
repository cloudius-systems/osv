/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "template.hh"

using namespace std;

template_source::template_source(std::ifstream& input)
    : _input(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>())
{
}

template_source::template_source(const std::string& input)
    : _input(input)
{
}

template_source::~template_source()
{
}

string template_source::expand(const map<string, string>& dict)
{
    auto result = _input;
    for (auto&& kv : dict) {
        result = substitute(result, "{{" + kv.first + "}}", kv.second);
    }
    return result;
}

string template_source::substitute(string str, string marker, string value)
{
    auto result = str;
    for (;;) {
        auto pos = result.find(marker);
        if (pos == std::string::npos) {
            break;
        }
        result = str.replace(pos, marker.length(), value);
    }
    return result;
}
