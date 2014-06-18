/*
 * Copyright (C) 2014 Cloudius Systems, Ltd.
 *
 * This work is open source software, licensed under the terms of the
 * BSD license as described in the LICENSE file in the top-level directory.
 */

#include "trace.hh"
#include "autogen/trace.json.hh"
#include "json/formatter.hh"

#include <string>
#include <algorithm>
#include <cctype>
#include <osv/tracecontrol.hh>

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
}
