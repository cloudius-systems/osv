/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "java_api.hh"

java_api* java_api::_instance = nullptr;
using namespace std;

void java_api::set(JavaVM_* jvm)
{

}

java_api& java_api::instance()
{
    if (_instance == nullptr) {
        _instance = new java_api();
    }
    return *_instance;
}

std::string java_api::get_mbean_info(const std::string& jmx_path)
{
    return "";
}

std::vector<std::string> java_api::get_all_mbean()
{
    return vector<string>();
}

std::vector<gc_info> java_api::get_all_gc()
{
    return std::vector<gc_info>();
}

std::string
java_api::get_system_property(const std::string& jmx_path)
{
    return "";
}

void java_api::set_mbean_info(const std::string& jmx_path,
        const std::string& attribute, const std::string& value)
{
}

void java_api::call_gc()
{
}

bool java_api::is_valid()
{
    return true;
}
