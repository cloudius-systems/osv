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
#include <osv/trace-count.hh>

#include <regex.h>

using namespace httpserver::json;
using namespace httpserver::json::trace_json;

static std::unordered_map<tracepoint_base*,
    std::unique_ptr<tracepoint_counter>> counters;

extern "C" void __attribute__((visibility("default"))) httpserver_plugin_register_routes(httpserver::routes* routes) {
    httpserver::api::trace::init(*routes);
}

/**
 * Initialize the routes object with specific routes mapping
 * @param routes - the routes object to fill
 */
void httpserver::api::trace::init(routes & routes)
{
    static auto matcher = [](const http::server::request & req) -> regex_t {
        const auto match = req.get_query_param("match");
        regex_t re;
        regcomp(&re, match.empty() ? ".*" : match.c_str(), REG_NOSUB);
        return re;
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

    trace_json_init_path("Trace API");

    trace_json::getTraceEventStatus.set_handler([](const_req req) {
        std::vector<jstrace_event_info> res;
        const auto re = matcher(req);
        for (auto & e : ::trace::get_event_info(&re)) {
            res.emplace_back(e);
        }
        return res;
    });
    trace_json::setTraceEventStatus.set_handler([](const_req req) {
        std::vector<jstrace_event_info> res;
        const auto enabled = str2bool(req.get_query_param("enabled"));
        const auto backtrace = str2bool(req.get_query_param("backtrace"));
        const auto re = matcher(req);
        for (auto & e : ::trace::set_event_state(&re, enabled, backtrace)) {
            res.emplace_back(e);
        }
        return res;
    });
    trace_json::getSingleTraceEventStatus.set_handler([](const_req req) {
        const auto eventid = req.param.at("eventid").substr(1);
        const auto e = ::trace::get_event_info(eventid);
        return jstrace_event_info(e);
    });
    trace_json::setSingleTraceEventStatus.set_handler([](const_req req) {
        const auto eventid = req.param.at("eventid").substr(1);
        const auto enabled = str2bool(req.get_query_param("enabled"));
        const auto backtrace = str2bool(req.get_query_param("backtrace"));
        const auto e = ::trace::set_event_state(eventid, enabled, backtrace);
        return jstrace_event_info(e);
    });

    trace_json::setSamplerState.set_handler([](const_req req) {
        auto freq = std::stoi(req.get_query_param("freq"));
        if (freq == 0) {
            prof::stop_sampler();
            return "Sampler stopped successfully";
        }

        const int max_frequency = 100000;
        if (freq > max_frequency) {
            throw bad_request_exception("Frequency too large. Maximum is " + std::to_string(max_frequency));
        }

        prof::config config;
        config.period = std::chrono::nanoseconds(1000000000 / freq);
        prof::start_sampler(config);
        return "Sampler started successfully";
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

    trace_json::setCountEvent.set_handler([](const_req req) {
        const auto eventid = req.param.at("eventid").substr(1);
        const auto enabled = str2bool(req.get_query_param("enabled"));
        bool found = false;
        for (auto & tp : tracepoint_base::tp_list) {
            if (eventid == std::string(tp.name)) {
                if (enabled) {
                    counters[&tp] = std::unique_ptr<tracepoint_counter>(
                            new tracepoint_counter(tp));
                } else {
                    counters.erase(&tp);
                }
                found = true;
            }
        }
        if (!found) {
            throw bad_request_exception("Unknown tracepoint name");
        }
        return "";
    });
    trace_json::getCounts.set_handler([](const_req req) {
        httpserver::json::TraceCounts ret;
        ret.time_ms = std::chrono::duration_cast<std::chrono::milliseconds>
            (osv::clock::uptime::now().time_since_epoch()).count();
        for (auto &it : counters) {
            TraceCount c;
            c.name = it.first->name;
            c.count = it.second->read();
            ret.list.push(c);
        }
        return ret;
    });
    trace_json::deleteCounts.set_handler([](const_req req) {
        counters.clear();
        return "";
    });

}
