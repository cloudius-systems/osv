/*
 * Copyright (C) 2013 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "handlers.hh"
#include "mime_types.hh"

#include <fstream>

namespace httpserver {

using namespace std;

const std::string handler_base::ERROR_500_PAGE("<h1>Something went wrong</h1>");
const std::string handler_base::ERROR_404_PAGE(
    "<h1>We didn't find the page you were looking for</h1>");

void handler_base::set_headers(http::server::reply& rep, const string& type)
{
    rep.status = http::server::reply::ok;
    rep.headers.resize(2);
    rep.headers[0].name = "Content-Length";
    rep.headers[0].value = to_string(rep.content.size());
    rep.headers[1].name = "Content-Type";
    rep.headers[1].value = http::server::mime_types::extension_to_type(type);
}

void handler_base::set_headers(http::server::reply& rep)
{
    set_headers(rep, "html");
}

void handler_base::reply400(http::server::reply& rep, int err_code,
                            const std::string& alternative_message)
{
    rep = http::server::reply::stock_reply(http::server::reply::not_found,
                                           &alternative_message);
}

void handler_base::reply500(http::server::reply& rep, int err_code,
                            const std::string& alternative_message)
{
    rep = http::server::reply::stock_reply(http::server::reply::bad_request,
                                           &alternative_message);
}

directory_handler::directory_handler(const string& doc_root)
    : doc_root(doc_root)
{
}

bool directory_handler::handle(const string& path, parameters* parts,
                               const http::server::request& req, http::server::reply& rep)
{
    // Determine the file extension.
    string relativePath = (*parts)["path"];
    string full_path = doc_root + relativePath;
    return read(full_path, req, rep);
}

string file_interaction_handler::get_extension(const string& file)
{
    size_t last_slash_pos = file.find_last_of("/");
    size_t last_dot_pos = file.find_last_of(".");
    string extension;
    if (last_dot_pos != string::npos && last_dot_pos > last_slash_pos) {
        extension = file.substr(last_dot_pos + 1);
    }
    return extension;
}

bool file_interaction_handler::read(const string& file,
                                    const http::server::request& req, http::server::reply& rep)
{
    ifstream is(file, ios::in | ios::binary);
    if (!is) {
        reply400(rep);
        return false;
    }

    string extension = get_extension(file);

    // Fill out the reply to be sent to the client.

    char buf[512];
    while (is.read(buf, sizeof(buf)).gcount() > 0)
        rep.content.append(buf, is.gcount());
    if (parser != nullptr) {
        parser->parse(rep.content, req, extension);
    }
    set_headers(rep, extension);
    return true;
}

bool file_handler::handle(const string& path, parameters* parts,
                          const http::server::request& req, http::server::reply& rep)
{
    return read(file, req, rep);
}

bool function_handler::handle(const string& path, parameters* parts,
                              const http::server::request& req, http::server::reply& rep)
{
    rep.content.append(f_handle(req));
    set_headers(rep, type);
    return true;
}

}
