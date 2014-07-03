/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include <boost/algorithm/string/replace.hpp>
#include "transformers.hh"

namespace httpserver {

using namespace std;

void content_replace::transform(std::string& content,
                                const http::server::request& req, const std::string& extension)
{
    string host = req.get_header("Host");
    if (host == "" || (this->extension != "" && extension != this->extension)) {
        return;
    }
    boost::replace_all(content, "{{Host}}", host);
}

}
