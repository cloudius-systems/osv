/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef _OSV_CLOUD_INIT_TEMPLATE_HH
#define _OSV_CLOUD_INIT_TEMPLATE_HH

#include <fstream>
#include <string>
#include <map>

/**
 * A template source that contains markers which are expanded to values
 * specified in a template dictionary.
 */
class template_source {
public:
    template_source(std::ifstream& input);
    template_source(const std::string& input);
    ~template_source();
    template_source(template_source const&) = delete;
    template_source& operator=(template_source const&) = delete;

    std::string expand(const std::map<std::string, std::string>& dict);

private:
    std::string substitute(std::string str, std::string marker, std::string value);

private:
    std::string _input;
};

#endif
