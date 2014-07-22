/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#ifndef CLOUDINIT_FILES_HH_
#define CLOUDINIT_FILES_HH_

#include <boost/filesystem.hpp>
#include <fstream>
#include "cloud-init.hh"

namespace fs = boost::filesystem;

class files_module : public init::config_module
{
public:
    static void create_file(const std::string& path, const std::string& content)
    {
        fs::create_directories(fs::path(path).parent_path());

        std::ofstream of(path, std::ofstream::out);
        if (!of) {
            throw std::runtime_error("Failed to create file: " + path);
        }

        of << content;
        if (!of) {
            throw std::runtime_error("Failed to write content of: " + path);
        }
    }

    virtual void handle(const YAML::Node& doc) override
    {
        for (auto& node : doc) {
            create_file(node.first.as<std::string>(), node.second.as<std::string>());
        }
    }

    virtual std::string get_label()
    {
        return "files";
    }
};

#endif
