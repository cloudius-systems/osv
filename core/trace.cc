#include "osv/trace.hh"
#include "sched.hh"
#include "arch.hh"
#include <atomic>
#include <regex>
#include <boost/algorithm/string/replace.hpp>

tracepoint<void*, void*> trace_function_entry("function entry", "fn %p caller %p");
tracepoint<void*, void*> trace_function_exit("function exit", "fn %p caller %p");

constexpr unsigned max_trace = 100000;

trace_record trace_log[max_trace];
std::atomic<unsigned> trace_record_last;
bool trace_enabled;

typeof(tracepoint_base::tp_list) tracepoint_base::tp_list __attribute__((init_priority(4000)));

void enable_trace()
{
    trace_enabled = true;
}

void enable_tracepoint(std::string wildcard)
{
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("*"), std::string(".*"));
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("?"), std::string("."));
    std::regex re{wildcard};
    for (auto& tp : tracepoint_base::tp_list) {
        if (std::regex_match(std::string(tp.name), re)) {
            tp.enable();
        }
    }
}

void tracepoint_base::enable()
{
    enabled = true;
}

trace_record* allocate_trace_record()
{
    unsigned p = trace_record_last.fetch_add(1, std::memory_order_relaxed);
    p %= max_trace;
    return &trace_log[p];
}

extern "C" void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    if (!trace_enabled) {
        return;
    }
    trace_function_entry(this_fn, call_site);
}

extern "C" void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
    if (!trace_enabled || !arch::tls_available()) {
        return;
    }
    trace_function_exit(this_fn, call_site);
}

