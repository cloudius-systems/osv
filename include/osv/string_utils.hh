/*
 * Copyright (C) 2024 Waldemar Kozaczuk
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef STRING_UTILS_HH
#define STRING_UTILS_HH

#include <string>
#include <vector>

namespace osv {

void split(std::vector<std::string> &output, const std::string& to_split, const char *delimiters, bool compress = false);

void replace_all(std::string &str, const std::string &from, const std::string &to);

}
#endif /* STRING_UTILS_HH */
