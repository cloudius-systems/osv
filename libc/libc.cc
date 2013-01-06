#include "libc.hh"
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <boost/algorithm/string/split.hpp>

int libc_error(int err)
{
    errno = err;
    return -1;
}

#undef errno

int __thread errno;

int* __errno_location()
{
    return &errno;
}

char* realpath(const char* path, char* resolved_path)
{
    // assumes cwd == /
    std::vector<std::string> components;
    std::string spath = path;
    boost::split(components, spath, [] (char c) { return c == '/'; });
    std::vector<std::string> tmp;
    for (auto c : components) {
        if (c == "" || c == ".") {
            continue;
        } else if (c == "..") {
            if (!tmp.empty()) {
                tmp.pop_back();
            }
        } else {
            tmp.push_back(c);
        }
    }
    std::string ret;
    for (auto c : tmp) {
        ret += "/" + c;
    }
    ret = ret.substr(0, PATH_MAX - 1);
    if (!resolved_path) {
        resolved_path = static_cast<char*>(malloc(ret.size() + 1));
    }
    strcpy(resolved_path, ret.c_str());
    return resolved_path;
}
