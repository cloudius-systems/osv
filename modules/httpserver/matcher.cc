/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "matcher.hh"

#include <iostream>
#include <string>

namespace httpserver {

using namespace std;

/**
 * Search for the end of the url parameter.
 * @param url the url to search
 * @param ind the position in the url
 * @param entire_path when set to true, take all the reminaing url
 * when set to false, search for the next slash
 * @return the position in the url of the end of the parameter
 */
static size_t find_end_param(const string& url, size_t ind, bool entire_path)
{
    size_t pos = (entire_path) ? url.length() : url.find('/', ind + 1);
    if (pos == string::npos) {
        return url.length();
    }
    return pos;
}

size_t param_matcher::match(const string& url, size_t ind, parameters& param)
{
    size_t last = find_end_param(url, ind, entire_path);

    if (last == 0) {
        /*
         * empty parameter allows only for the case of entire_path
         */
        return (entire_path) ? 0 : std::string::npos;
    }
    param[name] = url.substr(ind, last - ind);
    return last;
}

size_t str_matcher::match(const string& url, size_t ind, parameters& param)
{
    if (url.length() >= len + ind && (url.find(cmp, ind) == 0)
            && (url.length() == len + ind || url.at(len + ind) == '/')) {
        return len + ind;
    }
    return std::string::npos;
}

}
