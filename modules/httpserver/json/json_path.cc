/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "json_path.hh"

namespace httpserver {

namespace json {

using namespace std;

unordered_map<string, path_description*> path_description::path_by_nickname;

path_description* path_description::add_path(string path,
        operation_type method, string nickname)
{
    path_description* description = new path_description(path, method,
            nickname);

    path_by_nickname[description->operations.nickname] = description;
    return description;
}

path_description* path_description::get(string nickname)
{
    auto got = path_by_nickname.find(nickname);
    if (got == path_by_nickname.end())
        return nullptr;
    return got->second;
}

}

}
