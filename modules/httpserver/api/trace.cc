/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "trace.hh"
#include "autogen/trace.json.hh"
#include "json/formatter.hh"
#include "mime_types.hh"

#include <string>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <sys/stat.h>
#include <osv/tracecontrol.hh>
#include <osv/sampler.hh>

using namespace httpserver::json;
using namespace httpserver::json::trace_json;

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void httpserver::api::trace::init(routes & routes)
{
    static auto matcher = [](const http::server::request & req) -> std::regex {
        const auto match = req.get_query_param("match");
        return match.empty() ? std::regex(".*") : std::regex(match);
    };

    struct jstrace_event_info : public TraceEventInfo {
        jstrace_event_info(const ::trace::event_info & e)
        {
            this->enabled = e.enabled;
            this->backtrace = e.backtrace;
            this->id = e.id;
            this->name = e.name;
        }
    };

    struct str2bool {
        str2bool(std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            value = s == "true" || s == "1";
        }
        operator bool() const {
            return value;
        }
        bool value;
    };

    trace_json_init_path();

    trace_json::getTraceEventStatus.set_handler("json", [](const_req req) {
        std::vector<jstrace_event_info> res;
        for (auto & e : ::trace::get_event_info(matcher(req))) {
            res.emplace_back(e);
        }
        return formatter::to_json(res);
    });
    trace_json::setTraceEventStatus.set_handler("json", [](const_req req) {
        std::vector<jstrace_event_info> res;
        const auto enabled = str2bool(req.get_query_param("enabled"));
        const auto backtrace = str2bool(req.get_query_param("backtrace"));
        for (auto & e : ::trace::set_event_state(matcher(req), enabled, backtrace)) {
            res.emplace_back(e);
        }
        return formatter::to_json(res);
    });
    trace_json::getSingleTraceEventStatus.set_handler("json", [](const_req req) {
        const auto eventid = req.param.at("eventid").substr(1);
        const auto e = ::trace::get_event_info(eventid);
        return formatter::to_json(jstrace_event_info(e));
    });
    trace_json::setSingleTraceEventStatus.set_handler("json", [](const_req req) {
        const auto eventid = req.param.at("eventid").substr(1);
        const auto enabled = str2bool(req.get_query_param("enabled"));
        const auto backtrace = str2bool(req.get_query_param("backtrace"));
        const auto e = ::trace::set_event_state(eventid, enabled, backtrace);
        return formatter::to_json(jstrace_event_info(e));
    });

    trace_json::setSamplerState.set_handler("json", [](const_req req, http::server::reply& rep) {
        auto freq = std::stoi(req.get_query_param("freq"));
        if (freq == 0) {
            prof::stop_sampler();
            return formatter::to_json("Sampler stopped successfully");
        }

        const int max_frequency = 100000;
        if (freq > max_frequency) {
            rep.status = http::server::reply::status_type::bad_request;
            return formatter::to_json("Frequency too large. Maximum is " + std::to_string(max_frequency));
        }

        prof::config config;
        config.period = std::chrono::nanoseconds(1000000000 / freq);
        prof::start_sampler(config);
        return formatter::to_json("Sampler started successfully");
    });

    class create_trace_dump_file {
    public:
        create_trace_dump_file()
            : _filename(::trace::create_trace_dump()), _size(0)
        {
            struct stat st;
            if (::stat(_filename.c_str(), &st) != 0) {
                throw std::runtime_error("Could not create trace dump file");
            }
            _size = st.st_size;
        }
        ~create_trace_dump_file() {
            if (_size != 0) {
                // We are responsible for deleting the file
                ::unlink(_filename.c_str());
            }
        }
        operator const std::string &() const {
            return _filename;
        }
        size_t size() const {
            return _size;
        }
    private:
        const std::string _filename;
        size_t _size;
    };

    class create_trace_dump : public file_interaction_handler {
    public:
        void handle(const std::string& path, parameters* params,
                const http::server::request& req, http::server::reply& rep)
                        override {
            create_trace_dump_file dump;

            // TODO: at some point we can forsee files becoming
            // large enough that this very naive way of sending a file
            // via http will not work. Need to add streaming ability
            // to the server.
            rep.content.reserve(dump.size());
            read(dump, req, rep);
        }
    };

    trace_json::getTraceBuffers.set_handler(new create_trace_dump());
}
