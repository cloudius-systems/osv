/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "api.hh"
#include "autogen/api.json.hh"
#include <stdlib.h>
#include <functional>
#include <sstream>
#include "exception.hh"
#include "connection.hh"
#include "request_handler.hh"
#include "global_server.hh"

namespace httpserver {

namespace api {

namespace api {

using namespace json;
using namespace std;
using namespace api_json;

typedef std::function<
void(http::server::request&, const string&, http::server::reply&)> req_func;

class batch_parser {
public:
    bool parse(const string& batch, req_func f = nullptr);

    std::stringstream out;
    enum class states {
        START,
        OPEN_SB,
        OPEN_B,
        OPEN_TAG,
        CLOSE_TAG,
        COLON,
        OPEN_VAL,
        CLOSE_VAL,
        CLOSE_B,
        CLOSE_SB
    };
private:
    void set_params(http::server::request& req, string& request_path);
    char expect(std::string::const_iterator& it,
                const string::const_iterator& end, const string& s, states& state,
                states new_state);
    void end_quote(std::string::const_iterator& it,
                   const string::const_iterator& end, char end_char, states& state,
                   stringstream& current, states new_state);
};

char batch_parser::expect(string::const_iterator& it,
                          const string::const_iterator& end, const string& s, states& state,
                          states new_state)
{
    while (it != end && (*it == ' ' || *it == '\t')) {
        ++it;
    }
    if (it == end) {
        cout << "throwing expect" << endl;
        throw bad_param_exception("Bad batch request, missing entry");
    }
    if (s.find_first_of(*it) == string::npos) {
        throw bad_param_exception(
            string("Bad batch request ") + *it
            + " was found, when looking for " + s);
    }
    state = new_state;
    return *it;
}

void batch_parser::end_quote(string::const_iterator& it,
                             const string::const_iterator& end, char end_char, states& state,
                             stringstream& current, states new_state)
{
    for (; it != end; ++it) {
        if (*it == end_char) {
            state = new_state;
            return;
        }
        if (*it == '\\') {
            ++it;
        }
        current << *it;
    }
    cout << "throwing end_quote" << endl;
    throw bad_param_exception("Bad batch request unclosed open quote");
}

void batch_parser::set_params(http::server::request& req, string& request_path)
{
    // Decode url to path.
    size_t param = http::server::request_handler::update_parameters(req);
    if (!http::server::request_handler::url_decode(req.uri, request_path,
            param))
    {
        throw bad_param_exception(
            string("Invalid url encoding for ") + req.uri);
    }

}

bool batch_parser::parse(const string& batch, req_func f)
{
    states state = states::START;
    http::server::request req;
    string::const_iterator it;
    auto end = batch.end();
    stringstream current;
    string tag;
    out.str("");
    char quote = '"';
    for (it = batch.begin(); it != end; ++it) {
        switch (state) {
        case states::START:
            expect(it, end, "[", state, states::OPEN_SB);
            break;
        case states::OPEN_SB:
            expect(it, end, "{", state, states::OPEN_B);
            break;
        case states::OPEN_B:
            quote = expect(it, end, "'\"", state, states::OPEN_TAG);
            break;
        case states::OPEN_TAG:
            current.str("");
            end_quote(it, end, quote, state, current, states::CLOSE_TAG);
            tag = current.str();
            break;
        case states::CLOSE_TAG:
            expect(it, end, ":", state, states::COLON);
            break;
        case states::COLON:
            quote = expect(it, end, "'\"", state, states::OPEN_VAL);
            break;
        case states::OPEN_VAL:
            current.str("");
            end_quote(it, end, quote, state, current, states::CLOSE_VAL);
            if (tag == "method") {
                req.method = current.str();
            } else if (tag == "relative_url") {
                string val = current.str();
                if (val.size() == 0 || val[0] != '/') {
                    req.uri = '/' + val;
                } else {
                    req.uri = val;
                }
            }
            break;
        case states::CLOSE_VAL:
            quote = expect(it, end, ",}", state, states::OPEN_B);
            if (quote == '}') {
                state = states::CLOSE_B;
                if (f != nullptr) {
                    if (out.str().empty()) {
                        out << "[";
                    } else {
                        out << ",";
                    }
                    string path;
                    set_params(req, path);
                    http::server::reply _rep;
                    f(req, path, _rep);
                    out << "{ \"code\": " << _rep.status;
                    out << ", \"body\":";
                    out << _rep.content;
                    out << "}";
                }
            }
            break;
        case states::CLOSE_B:
            quote = expect(it, end, ",]", state, states::CLOSE_SB);
            if (quote == ',') {
                state = states::OPEN_SB;
            } else {
                out << "]";
            }
            break;
        default:
            break;
        }
    }
    if (state != states::CLOSE_SB) {
        throw bad_param_exception(
            "Bad batch request, the item list was not closed");
    }
    return false;
}

class api_param_handler : public handler_base {
public:
    api_param_handler(routes& _routes)
        : _routes(_routes)
    {
    }

    virtual void handle(const std::string& path, parameters* params,
                        const http::server::request& req, http::server::reply& rep)
    {
        req_func handle_req =
            [this](http::server::request& req, const string& path, http::server::reply& _rep)
        {
            _routes.handle(path, req, _rep);
        };
        http::server::connection_function when_done =
            [&rep, this, handle_req](http::server::connection& conn)
        {
            batch_parser parser;
            // we run the parser once, without doing anything just
            // to verify that the request itself is ok before
            // actually starting to perform the actions.
            try {
                parser.parse(conn.get_multipart_parser().stream.str());
                parser.parse(conn.get_multipart_parser().stream.str(), handle_req);
                rep.content = parser.out.str();
            } catch (const base_exception& _e) {
                json_exception e(_e);
                rep.content = e.to_json();
                rep.status = _e.status();
            } catch (exception& _e) {
                json_exception e(_e);
                cerr << "exception was caught for /api/batch: " << _e.what()
                     << endl;
                rep.content = e.to_json();
                rep.status = http::server::reply::internal_server_error;
            }

            set_headers(rep, "json");
        };
        req.connection_ptr->get_multipart_parser().set_call_back(
            http::server::multipart_parser::CLOSED, when_done);
        req.connection_ptr->upload();
    }
private:
    routes& _routes;
};

void init(routes& routes)
{
    api_json_init_path("Advanced API options");

    api_batch.set_handler(new api_param_handler(routes));
    stop_api.set_handler([](const_req req){
        global_server::stop();
        return "";
    });

}

}
}
}
