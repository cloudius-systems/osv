#include "osv/trace.hh"
#include "sched.hh"
#include "arch.hh"
#include <atomic>

enum class trace_record_type {
    invalid,
    entry,
    exit,
};

struct trace_record {
    trace_record_type type;
    sched::thread* thread;
    void* fn;
    void* caller;
};

constexpr unsigned max_trace = 100000;

trace_record trace_log[max_trace];
std::atomic<unsigned> trace_record_last;
bool trace_enabled;

void enable_trace()
{
    trace_enabled = true;
}

void add_trace_record(const trace_record& tr)
{
    unsigned p = trace_record_last.fetch_add(1, std::memory_order_relaxed);
    p %= max_trace;
    trace_log[p] = tr;
}

extern "C" void __cyg_profile_func_enter(void *this_fn, void *call_site)
{
    if (!trace_enabled) {
        return;
    }
    add_trace_record(trace_record{trace_record_type::entry, sched::thread::current(),
        this_fn, call_site});
}

extern "C" void __cyg_profile_func_exit(void *this_fn, void *call_site)
{
    if (!trace_enabled || !arch::tls_available()) {
        return;
    }
    add_trace_record(trace_record{trace_record_type::exit, sched::thread::current(),
        this_fn, call_site});
}

