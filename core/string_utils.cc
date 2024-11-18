/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <osv/string_utils.hh>

namespace osv {
void split(std::vector<std::string> &output, const std::string& to_split, const char *delimiters, bool compress)
{
    output.clear();
    size_t next = -1;
    do
    {
        if (compress) {
            next = to_split.find_first_not_of(delimiters, next + 1);
            if (next == std::string::npos) {
                break;
            }
            next -= 1;
        }
        size_t current = next + 1;
        next = to_split.find_first_of(delimiters, current);
        output.push_back(to_split.substr(current, next - current));
    } while (next != std::string::npos);
}

void replace_all(std::string &str, const std::string &from, const std::string &to)
{
    if (from == std::string("")) {
        return;
    }
    size_t start_pos = 0, from_length = from.length(), to_length = to.length();
    while ((start_pos = str.find(from, start_pos)) != std::string::npos)
    {
        str.replace(start_pos, from_length, to);
        start_pos += to_length;
    }
}
}
