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

// Generate a trace dump to temp file.
// Caller is responsible for deleting the file.
// This is a somewhat weird API, in a sense...
// The thought being that at some point we add
// the ability to make longer recordings, which
// we _really_ don't want fully in-memory.
// This should perhaps return a "smart object"
// providing a binary stream instead, but for now
// this is ok imho.
std::string
create_trace_dump();

struct symbol {
    std::string name;
    const void * addr;
    size_t size;
    const char * filename = 0;
    uint32_t n_locations = 0;

    virtual std::pair<uint32_t, int32_t> location(uint32_t) const {
        throw std::runtime_error("no locations");
    }
};

typedef std::function<void(const symbol &)> add_symbol_func;
typedef std::function<void(const add_symbol_func &)> generate_symbol_table_func;
typedef long generator_id;

generator_id
add_symbol_callback(const generate_symbol_table_func &);

void
remove_symbol_callback(generator_id);

}

#endif // TRACECONTROL_HH
