#include "osv/trace.hh"
#include "sched.hh"
#include "arch.hh"
#include <atomic>
#include <regex>
#include <boost/algorithm/string/replace.hpp>

tracepoint<void*, void*> trace_function_entry("function entry", "fn %p caller %p");
tracepoint<void*, void*> trace_function_exit("function exit", "fn %p caller %p");

constexpr size_t trace_page_size = 4096;  // need not match arch page size
constexpr unsigned max_trace = trace_page_size * 1024;

char trace_log[max_trace] __attribute__((may_alias, aligned(sizeof(long))));
std::atomic<size_t> trace_record_last;
bool trace_enabled;

typeof(tracepoint_base::tp_list) tracepoint_base::tp_list __attribute__((init_priority(4000)));

std::vector<std::regex> enabled_tracepoint_regexs;

void enable_trace()
{
    trace_enabled = true;
}

void enable_tracepoint(std::string wildcard)
{
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("*"), std::string(".*"));
    wildcard = boost::algorithm::replace_all_copy(wildcard, std::string("?"), std::string("."));
    std::regex re{wildcard};
    enabled_tracepoint_regexs.push_back(re);
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

void tracepoint_base::try_enable()
{
    for (auto& re : enabled_tracepoint_regexs) {
        if (std::regex_match(std::string(name), re)) {
            enable();
        }
    }
}

trace_record* allocate_trace_record(size_t size)
{
    size += sizeof(trace_record_base);
    size = align_up(size, sizeof(long));
    size_t p = trace_record_last.load(std::memory_order_relaxed);
    size_t pn;
    do {
        pn = p + size;
        if (align_down(p, trace_page_size) != align_down(pn, trace_page_size)) {
            // crossed page boundary
            pn = align_up(p, trace_page_size) + size;
        }
    } while (!trace_record_last.compare_exchange_weak(p, pn, std::memory_order_relaxed));
    char* pp = &trace_log[p % max_trace];
    // clear the first word, do indicate an padding at the end of the page
    reinterpret_cast<trace_record*>(pp)->tp = nullptr;
    pn -= size;
    return reinterpret_cast<trace_record*>(&trace_log[pn % max_trace]);
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

