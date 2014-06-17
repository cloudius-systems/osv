#ifndef TRACECONTROL_HH
#define TRACECONTROL_HH

#include <string>
#include <vector>
#include <regex>

class tracepoint_base;

namespace trace {

/**
 * Purposefully not including trace.hh nor making any reference
 * to types declared there. Both to keep this lean and to avoid
 * having to expand the include paths in httpserver.
 */

typedef std::string ext_id;

struct event_info {
    event_info(const tracepoint_base &);
    event_info(const event_info &) = default;

    ext_id      id;
    std::string name;
    bool        enabled;
    bool        backtrace;
};

// Maybe not declare here, but...
std::regex
glob_to_regex(const std::string &);

std::vector<event_info>
get_event_info();

event_info
get_event_info(const ext_id &);

std::vector<event_info>
get_event_info(const std::regex &);

event_info
set_event_state(const ext_id &, bool enable, bool stacktrace = false);

std::vector<event_info>
set_event_state(const std::regex &, bool enable, bool stacktrace = false);

event_info
set_event_state(tracepoint_base &, bool enable, bool stacktrace = false);


}

#endif // TRACECONTROL_HH
